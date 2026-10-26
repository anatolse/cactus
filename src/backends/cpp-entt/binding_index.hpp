#pragma once

#include "backends/cpp-entt/creation_order.hpp"

#include <entt/entt.hpp>
#include <raylib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

// ── Persistent pair-binding indexes ──────────────────────────────────────────
// A pair pass that reduces per one binding can look up the other binding's
// candidates in an index kept across passes, instead of snapshotting every
// member each pass. Indexes follow membership through EnTT construct/destroy
// signals on the traits that decide it; generated code reclassifies each
// marked entity before the pass reads the index. Candidates come back in
// creation order, so folds see their rows in the same order as a full scan.

// Changes whenever a world is initialized or replaced, so an index built for an
// earlier world rebuilds before its next use. Indexes see the program's own
// writes and structural changes; a host that writes trait fields directly calls
// advance_binding_index_epoch() afterwards.
[[nodiscard]] std::uint64_t binding_index_epoch() noexcept;
void advance_binding_index_epoch() noexcept;

using OrderedEntity = std::pair<std::uint64_t, entt::entity>;

// Closed-interval overlap of two bounds, the same test the sweep-and-prune
// broad phase uses.
[[nodiscard]] inline bool proxy_bounds_overlap(Vector3 a_min, Vector3 a_max, Vector3 b_min, Vector3 b_max) noexcept {
    return a_max.x >= b_min.x && b_max.x >= a_min.x && a_max.y >= b_min.y && b_max.y >= a_min.y &&
           a_max.z >= b_min.z && b_max.z >= a_min.z;
}

// The world and epoch something was built for.
struct WorldStamp {
    const entt::registry* registry = nullptr;
    std::uint64_t epoch            = 0;

    [[nodiscard]] bool stale(const entt::registry& current) const noexcept {
        return registry != &current || epoch != binding_index_epoch();
    }
    void stamp(const entt::registry& current) noexcept {
        registry = &current;
        epoch    = binding_index_epoch();
    }
};

// Which world an index was built for, and the entities whose membership may
// have changed since it last caught up. Every live tracker is listed, so a new
// epoch can drop what each one marked in the world it replaces.
class BindingIndexTracker {
public:
    BindingIndexTracker() noexcept { link(); }
    BindingIndexTracker(const BindingIndexTracker& other)
        : built_(other.built_), pending_(other.pending_), taken_(other.taken_) {
        link();
    }
    BindingIndexTracker(BindingIndexTracker&& other) noexcept
        : built_(other.built_), pending_(std::move(other.pending_)), taken_(std::move(other.taken_)) {
        link();
    }
    BindingIndexTracker& operator=(const BindingIndexTracker& other) {
        if (this == &other) {
            return *this;
        }
        built_   = other.built_;
        pending_ = other.pending_;
        taken_   = other.taken_;
        return *this;
    }
    BindingIndexTracker& operator=(BindingIndexTracker&& other) noexcept {
        built_   = other.built_;
        pending_ = std::move(other.pending_);
        taken_   = std::move(other.taken_);
        return *this;
    }
    ~BindingIndexTracker() { unlink(); }

    // Frees what every live tracker has marked; called when the epoch advances.
    static void forget_all_pending() noexcept;

    [[nodiscard]] bool stale(const entt::registry& registry) const noexcept { return built_.stale(registry); }

    void reset(const entt::registry& registry) {
        built_.stamp(registry);
        pending_.clear();
    }

    // Signal listener for on_construct / on_destroy of a watched trait.
    // A stale index rebuilds from every member, so it ignores changes until then.
    void on_signal(entt::registry& /*registry*/, entt::entity entity) {
        if (built_.epoch == binding_index_epoch()) {
            pending_.push_back(entity);
        }
    }

    void mark(entt::entity entity) { pending_.push_back(entity); }

    // The marked entities, each once, and forgets them. Valid until the next take.
    [[nodiscard]] const std::vector<entt::entity>& take_pending() {
        taken_.swap(pending_);
        pending_.clear();
        std::ranges::sort(taken_);
        const auto [first, last] = std::ranges::unique(taken_);
        taken_.erase(first, last);
        return taken_;
    }

    // Ends a catch-up: invalidates the taken entities and frees the buffer of a
    // large one, such as a world's first build.
    void settle() {
        if (taken_.capacity() > kKeptBuffer) {
            taken_ = std::vector<entt::entity>{};
        }
        taken_.clear();
    }

private:
    static constexpr std::size_t kKeptBuffer = 1024;

    void link() noexcept;
    void unlink() noexcept;

    WorldStamp built_;
    std::vector<entt::entity> pending_;
    std::vector<entt::entity> taken_;
    BindingIndexTracker* previous_ = nullptr;
    BindingIndexTracker* next_     = nullptr;
};

// One index per Key: passes whose generated index code is identical share it,
// so each catches up once and keeps one copy of the members.
template <typename Index, std::uint64_t Key>
[[nodiscard]] Index& shared_binding_index() {
    static Index index;
    return index;
}

// Connects `tracker` to construct and destroy of every trait in Traits,
// replacing any earlier connection of the same tracker on those storages.
template <typename... Traits>
void watch_binding_traits(entt::registry& registry, BindingIndexTracker& tracker) {
    (
        [&] {
            registry.on_construct<Traits>().disconnect(&tracker);
            registry.on_destroy<Traits>().disconnect(&tracker);
            registry.on_construct<Traits>().template connect<&BindingIndexTracker::on_signal>(tracker);
            registry.on_destroy<Traits>().template connect<&BindingIndexTracker::on_signal>(tracker);
        }(),
        ...);
}

// A paged map from an entity's index to a uint32 value, with pages made on
// demand, as in EnTT's sparse sets. Versions are not compared: callers check
// the handle they stored.
class SparseSlots {
public:
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] std::uint32_t get(entt::entity entity) const noexcept {
        const auto index = page_index(entity);
        const auto page  = index / kPageSize;
        return page < pages_.size() && pages_[page] != nullptr ? (*pages_[page])[index % kPageSize] : kNone;
    }

    void set(entt::entity entity, std::uint32_t value) {
        const auto index = page_index(entity);
        const auto page  = index / kPageSize;
        if (page >= pages_.size()) {
            pages_.resize(page + 1);
        }
        if (pages_[page] == nullptr) {
            pages_[page] = std::make_unique_for_overwrite<Page>();
            pages_[page]->fill(kNone);
            ++page_count_;
        }
        (*pages_[page])[index % kPageSize] = value;
    }

    void erase(entt::entity entity) noexcept {
        const auto index = page_index(entity);
        const auto page  = index / kPageSize;
        if (page < pages_.size() && pages_[page] != nullptr) {
            (*pages_[page])[index % kPageSize] = kNone;
        }
    }

    void clear() noexcept {
        pages_.clear();
        page_count_ = 0;
    }

    [[nodiscard]] std::size_t page_count() const noexcept { return page_count_; }

private:
    static constexpr std::size_t kPageSize = 1024;
    using Page                             = std::array<std::uint32_t, kPageSize>;

    [[nodiscard]] static std::size_t page_index(entt::entity entity) noexcept {
        return static_cast<std::size_t>(entt::to_entity(entity));
    }

    std::vector<std::unique_ptr<Page>> pages_;
    std::size_t page_count_ = 0;
};

// Chains store full handles as uint32; the null handle doubles as the end mark.
static_assert(entt::to_integral(entt::entity{entt::null}) == SparseSlots::kNone);

[[nodiscard]] inline std::uint32_t entity_bits(entt::entity entity) noexcept {
    return static_cast<std::uint32_t>(entt::to_integral(entity));
}
[[nodiscard]] inline entt::entity entity_from_bits(std::uint32_t bits) noexcept {
    return entt::entity{bits};
}

// Static members' 3D bounds in CSR buckets over hashed cubic cells, shared by up to 32 owners.
class StaticGrid3D {
public:
    static constexpr float kMinGridCellSize = 1e-3F;
    // Room for rounding and proxy skin on members of the requested size.
    static constexpr float kCellSlack = 1.0625F;
    static constexpr std::uint32_t kAllOwners = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t kMaxOwners   = 32;

    [[nodiscard]] static constexpr std::uint32_t owner_mask(std::size_t owner) noexcept {
        return std::uint32_t{1} << owner;
    }

    // A non-positive or non-finite size is chosen at the first merge from the members' median extent.
    void clear(float cell_size);
    // Whether `owner` did not hold the member before. Bounds that differ from
    // the stored ones refile the member for every owner.
    bool insert(entt::entity entity, Vector3 min, Vector3 max, std::size_t owner = 0);
    // Whether `owner` held the member.
    bool remove(entt::entity entity, std::size_t owner = 0);
    // Appends every member held by one of `owners` whose bounds overlap
    // [min, max], in no order and possibly more than once.
    void query(Vector3 min, Vector3 max, std::vector<entt::entity>& out, std::uint32_t owners = kAllOwners);
    [[nodiscard]] bool contains(entt::entity entity) const noexcept { return find(entity) != SparseSlots::kNone; }
    [[nodiscard]] std::size_t size() const noexcept { return live_; }
    [[nodiscard]] float cell_size() const noexcept { return cell_size_; }

private:
    struct Member {
        entt::entity entity{entt::null};
        std::uint32_t owners = 0;
        Vector3 min{};
        Vector3 max{};
    };

    [[nodiscard]] std::uint32_t find(entt::entity entity) const noexcept;
    void file_slot(std::uint32_t slot);
    void drop(std::uint32_t slot);
    void set_cell_size(float cell_size) noexcept;
    [[nodiscard]] bool merge_due() const noexcept;
    void merge();
    void choose_cell_size();
    void scan_cells(Vector3 min, Vector3 max, std::uint32_t owners, std::vector<entt::entity>& out) const;
    [[nodiscard]] std::size_t bucket_of(std::int64_t x, std::int64_t y, std::int64_t z) const noexcept;
    // Visits each cell `member` is filed in; false when it is kept aside.
    template <typename Visit>
    bool for_each_filing(const Member& member, const Visit& visit) const;

    float cell_size_     = 1.0F;
    double inverse_cell_ = 1.0;
    bool size_pending_   = false;
    std::vector<Member> members_;  // [0, merged_) are filed; the rest wait in the side list
    std::size_t merged_ = 0;
    std::size_t live_   = 0;
    std::size_t dead_   = 0;
    SparseSlots slots_;
    std::vector<std::uint32_t> conflicts_;  // live members whose entity index slots_ gives another version
    std::vector<std::uint32_t> offsets_;    // per bucket, its first filing; one extra end mark
    std::vector<std::uint32_t> filed_;      // member slots, by bucket
    std::vector<std::uint32_t> aside_;
};

// A static grid that spatial indexes with the same bounds code share. It is
// cleared once per world; each index that then resets joins as a new owner.
class StaticStore3D {
public:
    // The owner number of the joining index.
    std::size_t join(const entt::registry& registry, float cell_size) {
        if (built_.stale(registry)) {
            built_.stamp(registry);
            grid_.clear(cell_size);
            owners_ = 0;
        }
        return std::min(owners_++, StaticGrid3D::kMaxOwners - 1);
    }

    [[nodiscard]] StaticGrid3D& grid() noexcept { return grid_; }

private:
    WorldStamp built_;
    std::size_t owners_ = 0;
    StaticGrid3D grid_;
};

// Largest extents of collider shapes, and the median a grid cell size is taken from.
[[nodiscard]] constexpr float box_extent(Vector3 size) noexcept {
    return std::max({size.x, size.y, size.z});
}
[[nodiscard]] constexpr float sphere_extent(float radius) noexcept {
    return 2.0F * radius;
}
[[nodiscard]] constexpr float capsule_extent(float radius, float height) noexcept {
    return std::max(2.0F * radius, height);
}
template <std::size_t N>
[[nodiscard]] constexpr float median_extent(std::array<float, N> extents) noexcept {
    std::ranges::sort(extents);
    return extents[N / 2];
}

template <std::uint64_t Key>
[[nodiscard]] StaticStore3D& shared_static_store() {
    static StaticStore3D store;
    return store;
}

// One pair binding's members, split by whether their bounds can change: static
// members live in a shared grid; dynamic members are listed in creation order
// and their bounds recomputed by each pass.
class SpatialBindingIndex3D {
public:
    [[nodiscard]] BindingIndexTracker& tracker() noexcept { return tracker_; }

    // A member's bounds, or nullopt for an entity that is no member or has no
    // bounds (it can only miss).
    struct Classified {
        bool member    = false;
        bool is_static = false;
        std::optional<std::pair<Vector3, Vector3>> bounds;
    };

    void reset(const entt::registry& registry, StaticStore3D& store, float cell_size) {
        tracker_.reset(registry);
        owner_        = store.join(registry, cell_size);
        grid_         = &store.grid();
        static_count_ = 0;
        dynamic_.clear();
    }

    // Reclassifies every marked entity. Classify(entt::entity) -> Classified.
    template <typename Classify>
    void catch_up(const entt::registry& registry, const Classify& classify) {
        for (const auto entity : tracker_.take_pending()) {
            if (grid_->remove(entity, owner_)) {
                --static_count_;
            }
            std::erase_if(dynamic_, [&](const OrderedEntity& member) { return member.second == entity; });
            if (!registry.valid(entity)) {
                continue;
            }
            const Classified found = classify(entity);
            if (!found.member) {
                continue;
            }
            const auto ordinal = creation_ordinal(registry, entity);
            if (!found.is_static) {
                dynamic_.insert(std::ranges::upper_bound(dynamic_, OrderedEntity{ordinal, entity}),
                                OrderedEntity{ordinal, entity});
                continue;
            }
            if (found.bounds.has_value() && grid_->insert(entity, found.bounds->first, found.bounds->second, owner_)) {
                ++static_count_;
            }
        }
        tracker_.settle();
    }

    using Bounds        = std::pair<Vector3, Vector3>;
    using BoundedMember = std::pair<OrderedEntity, Bounds>;

    // Appends the static members whose bounds overlap [min, max], each once, oldest first.
    void query_static(const entt::registry& registry, Vector3 min, Vector3 max, std::vector<OrderedEntity>& out) {
        found_.clear();
        grid_->query(min, max, found_, StaticGrid3D::owner_mask(owner_));
        std::ranges::sort(found_);
        const auto duplicates = std::ranges::unique(found_).begin();
        const auto first      = std::ssize(out);
        for (auto entity = found_.begin(); entity != duplicates; ++entity) {
            out.emplace_back(creation_ordinal(registry, *entity), *entity);
        }
        std::sort(out.begin() + first, out.end());
        if (found_.capacity() > kKeptScratch) {
            found_ = std::vector<entt::entity>{};
        }
    }

    // Replaces `out` with the dynamic members and their bounds this pass,
    // oldest first. BoundsOf(entt::entity) -> std::optional<Bounds>; a member
    // without bounds is left out.
    template <typename BoundsOf>
    void bound_dynamic(const BoundsOf& bounds_of, std::vector<BoundedMember>& out) const {
        out.clear();
        for (const auto& member : dynamic_) {
            if (const std::optional<Bounds> bounds = bounds_of(member.second); bounds.has_value()) {
                out.emplace_back(member, *bounds);
            }
        }
    }

    // Replaces `out` with the members whose bounds overlap [min, max], oldest
    // first: static members from the grid and `dynamic` from bound_dynamic().
    void candidates(const entt::registry& registry,
                    Vector3 min,
                    Vector3 max,
                    const std::vector<BoundedMember>& dynamic,
                    std::vector<OrderedEntity>& out) {
        out.clear();
        query_static(registry, min, max, out);
        const auto static_count = std::ssize(out);
        for (const auto& [member, bounds] : dynamic) {
            if (proxy_bounds_overlap(min, max, bounds.first, bounds.second)) {
                out.push_back(member);
            }
        }
        std::ranges::inplace_merge(out, out.begin() + static_count);
    }

    // Dynamic members, oldest first.
    [[nodiscard]] const std::vector<OrderedEntity>& dynamic_members() const noexcept { return dynamic_; }
    [[nodiscard]] std::size_t static_size() const noexcept { return static_count_; }

private:
    static constexpr std::size_t kKeptScratch = 4096;

    BindingIndexTracker tracker_;
    StaticGrid3D* grid_       = nullptr;
    std::size_t owner_        = 0;
    std::size_t static_count_ = 0;
    std::vector<OrderedEntity> dynamic_;
    std::vector<entt::entity> found_;
};

// Forgets every marked entity, then files each one that is still a member,
// oldest first, so a chain only ever reads the ordinals of live members.
// KeyOf(entt::entity) -> std::optional<Key>; Insert(entity, ordinal, key).
template <typename Erase, typename KeyOf, typename Insert>
void catch_up_in_creation_order(BindingIndexTracker& tracker,
                                const entt::registry& registry,
                                const Erase& erase,
                                const KeyOf& key_of,
                                const Insert& insert) {
    using Key           = std::invoke_result_t<KeyOf, entt::entity>::value_type;
    const auto& pending = tracker.take_pending();
    for (const auto entity : pending) {
        erase(entity);
    }
    std::vector<std::pair<OrderedEntity, Key>> joining;
    joining.reserve(pending.size());
    for (const auto entity : pending) {
        if (!registry.valid(entity)) {
            continue;
        }
        if (std::optional<Key> key = key_of(entity); key.has_value()) {
            joining.emplace_back(OrderedEntity{creation_ordinal(registry, entity), entity}, *key);
        }
    }
    tracker.settle();
    std::ranges::sort(joining, {}, [](const auto& entry) { return entry.first; });
    for (const auto& [member, key] : joining) {
        insert(member.second, member.first, key);
    }
}

// Splices `entity` into an oldest-first chain. NextOf(bits) -> bits; SetNext(bits, bits).
template <typename NextOf, typename SetNext>
void chain_insert(const entt::registry& registry,
                  std::uint32_t& head,
                  entt::entity entity,
                  std::uint64_t ordinal,
                  const NextOf& next_of,
                  const SetNext& set_next) {
    auto previous = SparseSlots::kNone;
    auto current  = head;
    while (current != SparseSlots::kNone && creation_ordinal(registry, entity_from_bits(current)) < ordinal) {
        previous = current;
        current  = next_of(current);
    }
    const auto bits = entity_bits(entity);
    set_next(bits, current);
    if (previous == SparseSlots::kNone) {
        head = bits;
    } else {
        set_next(previous, bits);
    }
}

// Unlinks `bits` from a chain; false when it is not in it.
template <typename NextOf, typename SetNext>
bool chain_unlink(std::uint32_t& head, std::uint32_t bits, const NextOf& next_of, const SetNext& set_next) {
    auto previous = SparseSlots::kNone;
    auto current  = head;
    while (current != SparseSlots::kNone && current != bits) {
        previous = current;
        current  = next_of(current);
    }
    if (current == SparseSlots::kNone) {
        return false;
    }
    const auto after = next_of(bits);
    if (previous == SparseSlots::kNone) {
        head = after;
    } else {
        set_next(previous, after);
    }
    return true;
}

// An equality index over keys whose parts all have known ranges: one row per
// value combination in a block per container entity. Members that cannot be
// addressed wait in an overflow list every query scans.
template <std::size_t Parts>
class DenseKeyIndex {
public:
    // [lo, hi) per part.
    using Extents = std::array<std::pair<std::int64_t, std::int64_t>, Parts>;
    struct Key {
        entt::entity block{entt::null};
        std::array<std::int64_t, Parts> values{};
    };

    [[nodiscard]] BindingIndexTracker& tracker() noexcept { return tracker_; }

    void reset(const entt::registry& registry) {
        tracker_.reset(registry);
        rows_.clear();
        next_.clear();
        block_of_.clear();
        blocks_.clear();
        heads_.clear();
        overflow_.clear();
        unblocked_ = kNone;
        size_      = 0;
    }

    // KeyOf(entt::entity) -> std::optional<Key>, nullopt for a non-member.
    // ExtentsOf(entt::entity block) -> std::optional<Extents>, read when a
    // block gets its first member.
    template <typename KeyOf, typename ExtentsOf>
    void catch_up(const entt::registry& registry, const KeyOf& key_of, const ExtentsOf& extents_of) {
        catch_up_in_creation_order(
            tracker_, registry, [&](entt::entity entity) { erase(entity); }, key_of,
            [&](entt::entity entity, std::uint64_t ordinal, const Key& key) {
                insert(registry, entity, ordinal, key, extents_of);
            });
    }

    // Replaces `out` with the members whose key equals `key`, oldest first.
    void find(const entt::registry& registry, const Key& key, std::vector<entt::entity>& out) const {
        const auto block = key.block == entt::null ? unblocked_ : find_block(key.block);
        gather(registry, key.values, block == kNone ? std::span<const Block>{} : std::span{&blocks_[block], 1},
               [&](const Key& waiting) { return waiting.block == key.block; }, out);
    }

    // Replaces `out` with the members of any block whose values equal
    // `values`, oldest first.
    void find_any_block(const entt::registry& registry,
                        const std::array<std::int64_t, Parts>& values,
                        std::vector<entt::entity>& out) const {
        gather(registry, values, blocks_, [](const Key&) { return true; }, out);
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    static constexpr std::uint32_t kNone     = SparseSlots::kNone;
    static constexpr std::uint32_t kOverflow = kNone - 1;
    // A block needing more rows than this is not addressed; its members wait in the overflow list.
    static constexpr std::uint64_t kMaxBlockRows = std::uint64_t{1} << 24U;

    struct Block {
        entt::entity handle{entt::null};
        std::uint32_t base = 0;
        std::uint32_t rows = 0;
        Extents extents{};
    };
    struct Waiting {
        entt::entity entity{entt::null};
        std::uint64_t ordinal = 0;
        Key key;
    };

    [[nodiscard]] static std::uint32_t row_in(const Block& block, const std::array<std::int64_t, Parts>& values) noexcept {
        std::uint64_t offset = 0;
        for (std::size_t part = 0; part < Parts; ++part) {
            const auto [lo, hi] = block.extents[part];
            if (values[part] < lo || values[part] >= hi) {
                return kNone;
            }
            offset = (offset * static_cast<std::uint64_t>(hi - lo)) + static_cast<std::uint64_t>(values[part] - lo);
        }
        return block.base + static_cast<std::uint32_t>(offset);
    }

    // Rows for these extents, or nullopt when they are too many to address.
    [[nodiscard]] static std::optional<std::uint32_t> row_count(const Extents& extents) noexcept {
        std::uint64_t rows = 1;
        for (const auto& [lo, hi] : extents) {
            const auto span = hi > lo ? static_cast<std::uint64_t>(hi - lo) : 0U;
            if (span != 0 && rows > kMaxBlockRows / span) {
                return std::nullopt;
            }
            rows *= span;
        }
        return static_cast<std::uint32_t>(rows);
    }

    template <typename BlockMatches>
    void gather(const entt::registry& registry,
                const std::array<std::int64_t, Parts>& values,
                std::span<const Block> blocks,
                const BlockMatches& block_matches,
                std::vector<entt::entity>& out) const {
        out.clear();
        std::size_t sources = 0;
        for (const auto& block : blocks) {
            const auto row = row_in(block, values);
            if (row == kNone || heads_[row] == kNone) {
                continue;
            }
            ++sources;
            for (auto member = heads_[row]; member != kNone; member = next_.get(entity_from_bits(member))) {
                out.push_back(entity_from_bits(member));
            }
        }
        const auto chained = out.size();
        for (const auto& member : overflow_) {
            if (member.key.values == values && block_matches(member.key)) {
                out.push_back(member.entity);
            }
        }
        sources += out.size() > chained ? 1U : 0U;
        if (sources > 1) {
            std::ranges::sort(out, {}, [&](entt::entity entity) { return creation_ordinal(registry, entity); });
        }
    }

    [[nodiscard]] std::uint32_t find_block(entt::entity handle) const noexcept {
        const auto block = block_of_.get(handle);
        return block != kNone && blocks_[block].handle == handle ? block : kNone;
    }

    [[nodiscard]] bool region_empty(const Block& block) const noexcept {
        return std::all_of(heads_.begin() + block.base, heads_.begin() + block.base + block.rows,
                           [](std::uint32_t head) { return head == kNone; });
    }

    // Sizes `block` for `handle`, reusing its rows when they are enough.
    template <typename ExtentsOf>
    [[nodiscard]] bool size_block(Block& block, entt::entity handle, const ExtentsOf& extents_of, bool reuse) {
        const std::optional<Extents> extents = extents_of(handle);
        const auto rows = extents.has_value() ? row_count(*extents) : std::nullopt;
        if (!rows.has_value()) {
            return false;
        }
        if (!reuse || *rows > block.rows) {
            if (heads_.size() + *rows >= kOverflow) {
                return false;
            }
            block.base = static_cast<std::uint32_t>(heads_.size());
            heads_.resize(heads_.size() + *rows, kNone);
        }
        block.handle  = handle;
        block.rows    = *rows;
        block.extents = *extents;
        return true;
    }

    // The block of `handle`, made on first use; kNone when it cannot be addressed.
    template <typename ExtentsOf>
    [[nodiscard]] std::uint32_t block_for(entt::entity handle, const ExtentsOf& extents_of) {
        auto known = handle == entt::null ? unblocked_ : block_of_.get(handle);
        if (known != kNone) {
            auto& block = blocks_[known];
            if (block.handle == handle) {
                return known;
            }
            // Another version of this entity index owns the block; reuse it once it is empty.
            if (!region_empty(block) || !size_block(block, handle, extents_of, true)) {
                return kNone;
            }
            return known;
        }
        Block block;
        if (!size_block(block, handle, extents_of, false)) {
            return kNone;
        }
        known = static_cast<std::uint32_t>(blocks_.size());
        blocks_.push_back(block);
        if (handle == entt::null) {
            unblocked_ = known;
        } else {
            block_of_.set(handle, known);
        }
        return known;
    }

    template <typename ExtentsOf>
    void insert(const entt::registry& registry,
                entt::entity entity,
                std::uint64_t ordinal,
                const Key& key,
                const ExtentsOf& extents_of) {
        ++size_;
        const auto block = block_for(key.block, extents_of);
        const auto row   = block == kNone ? kNone : row_in(blocks_[block], key.values);
        if (row == kNone) {
            const Waiting waiting{.entity = entity, .ordinal = ordinal, .key = key};
            overflow_.insert(std::ranges::upper_bound(overflow_, ordinal, {}, &Waiting::ordinal), waiting);
            rows_.set(entity, kOverflow);
            return;
        }
        rows_.set(entity, row);
        chain_insert(registry, heads_[row], entity, ordinal, next_of(), set_next());
    }

    [[nodiscard]] auto next_of() const noexcept {
        return [this](std::uint32_t member) { return next_.get(entity_from_bits(member)); };
    }
    [[nodiscard]] auto set_next() noexcept {
        return [this](std::uint32_t member, std::uint32_t next) { next_.set(entity_from_bits(member), next); };
    }

    void erase(entt::entity entity) {
        const auto row = rows_.get(entity);
        if (row == kNone) {
            return;
        }
        if (row == kOverflow) {
            const auto found = std::ranges::find(overflow_, entity, &Waiting::entity);
            if (found != overflow_.end()) {
                overflow_.erase(found);
                rows_.erase(entity);
                --size_;
            }
            return;
        }
        if (!chain_unlink(heads_[row], entity_bits(entity), next_of(), set_next())) {
            return;
        }
        rows_.erase(entity);
        next_.erase(entity);
        --size_;
    }

    BindingIndexTracker tracker_;
    SparseSlots rows_;
    SparseSlots next_;
    SparseSlots block_of_;
    std::vector<Block> blocks_;
    std::vector<std::uint32_t> heads_;
    std::vector<Waiting> overflow_;
    std::uint32_t unblocked_ = kNone;
    std::size_t size_        = 0;
};

// An equality index over keys with a part no range bounds: an open-addressing
// table from key to its oldest member, the rest chained oldest first.
template <std::size_t K>
class FlatKeyIndex {
public:
    using Key = std::array<std::uint64_t, K>;

    [[nodiscard]] BindingIndexTracker& tracker() noexcept { return tracker_; }

    void reset(const entt::registry& registry) {
        tracker_.reset(registry);
        table_.clear();
        members_.clear();
        slot_of_.clear();
        used_ = 0;
        keys_ = 0;
    }

    // KeyOf(entt::entity) -> std::optional<Key>, nullopt for a non-member.
    template <typename KeyOf>
    void catch_up(const entt::registry& registry, const KeyOf& key_of) {
        catch_up_in_creation_order(
            tracker_, registry, [&](entt::entity entity) { erase(entity); }, key_of,
            [&](entt::entity entity, std::uint64_t ordinal, const Key& key) { insert(registry, entity, ordinal, key); });
    }

    // Replaces `out` with the members whose key equals `key`, oldest first.
    void find(const entt::registry& /*registry*/, const Key& key, std::vector<entt::entity>& out) const {
        out.clear();
        const auto slot = lookup(key);
        if (slot == kNone) {
            return;
        }
        for (auto member = table_[slot].head; member != kNone; member = next_of(member)) {
            out.push_back(entity_from_bits(member));
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return members_.size(); }

private:
    static constexpr std::uint32_t kNone      = SparseSlots::kNone;
    static constexpr std::uint32_t kTombstone = kNone - 1;

    struct Slot {
        Key key{};
        std::uint32_t head = kNone;  // kNone: never used; kTombstone: its key left
    };
    struct Entry {
        entt::entity entity{entt::null};
        std::uint32_t next = kNone;
        Key key{};
    };

    [[nodiscard]] static std::size_t hash(const Key& key) noexcept {
        std::uint64_t value = 1469598103934665603ULL;
        for (const auto part : key) {
            value = (value ^ part) * 1099511628211ULL;
            value ^= value >> 29U;
        }
        return static_cast<std::size_t>(value);
    }

    [[nodiscard]] std::uint32_t lookup(const Key& key) const noexcept {
        if (table_.empty()) {
            return kNone;
        }
        const auto mask = table_.size() - 1;
        for (auto slot = hash(key) & mask;; slot = (slot + 1) & mask) {
            if (table_[slot].head == kNone) {
                return kNone;
            }
            if (table_[slot].head != kTombstone && table_[slot].key == key) {
                return static_cast<std::uint32_t>(slot);
            }
        }
    }

    [[nodiscard]] std::uint32_t next_of(std::uint32_t member) const noexcept {
        return members_[slot_of_.get(entity_from_bits(member))].next;
    }

    void set_next(std::uint32_t member, std::uint32_t next) noexcept {
        members_[slot_of_.get(entity_from_bits(member))].next = next;
    }

    void rehash(std::size_t capacity) {
        std::vector<Slot> old = std::exchange(table_, std::vector<Slot>(capacity));
        used_                 = 0;
        for (const auto& slot : old) {
            if (slot.head != kNone && slot.head != kTombstone) {
                table_[claim(slot.key)] = slot;
            }
        }
    }

    // The slot for a key not in the table: the first unused or tombstoned one.
    [[nodiscard]] std::size_t claim(const Key& key) {
        const auto mask = table_.size() - 1;
        auto slot       = hash(key) & mask;
        while (table_[slot].head != kNone && table_[slot].head != kTombstone) {
            slot = (slot + 1) & mask;
        }
        used_ += table_[slot].head == kNone ? 1U : 0U;
        return slot;
    }

    void insert(const entt::registry& registry, entt::entity entity, std::uint64_t ordinal, const Key& key) {
        auto slot = lookup(key);
        if (slot == kNone) {
            if ((used_ + 1) * 2 > table_.size()) {
                rehash(std::max<std::size_t>(16, std::bit_ceil((keys_ + 1) * 4)));
            }
            slot              = static_cast<std::uint32_t>(claim(key));
            table_[slot].key  = key;
            table_[slot].head = kNone;
            ++keys_;
        }
        slot_of_.set(entity, static_cast<std::uint32_t>(members_.size()));
        members_.push_back(Entry{.entity = entity, .next = kNone, .key = key});
        chain_insert(registry, table_[slot].head, entity, ordinal, chain_next(), chain_set_next());
    }

    [[nodiscard]] auto chain_next() const noexcept {
        return [this](std::uint32_t member) { return next_of(member); };
    }
    [[nodiscard]] auto chain_set_next() noexcept {
        return [this](std::uint32_t member, std::uint32_t next) { set_next(member, next); };
    }

    void erase(entt::entity entity) {
        const auto index = slot_of_.get(entity);
        if (index == kNone || index >= members_.size() || members_[index].entity != entity) {
            return;
        }
        const auto slot = lookup(members_[index].key);
        chain_unlink(table_[slot].head, entity_bits(entity), chain_next(), chain_set_next());
        if (table_[slot].head == kNone) {
            table_[slot].head = kTombstone;
            --keys_;
        }
        if (index + 1 != members_.size()) {
            members_[index] = members_.back();
            slot_of_.set(members_[index].entity, index);
        }
        members_.pop_back();
        slot_of_.erase(entity);
    }

    BindingIndexTracker tracker_;
    std::vector<Slot> table_;
    std::vector<Entry> members_;
    SparseSlots slot_of_;
    std::size_t used_ = 0;  // slots ever claimed since the last rehash, tombstones included
    std::size_t keys_ = 0;
};

// Orders the members several lookups found oldest first, each once.
inline void merge_equality_candidates(const entt::registry& registry, std::vector<entt::entity>& candidates) {
    std::ranges::sort(candidates, {}, [&](entt::entity entity) { return creation_ordinal(registry, entity); });
    const auto [first, last] = std::ranges::unique(candidates);
    candidates.erase(first, last);
}

// An equality-key component for one value: ints by their bits, entities by
// their full handle, bools as 0 or 1.
[[nodiscard]] inline std::uint64_t equality_key_part(int value) noexcept {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(value));
}
[[nodiscard]] inline std::uint64_t equality_key_part(bool value) noexcept {
    return value ? 1U : 0U;
}
[[nodiscard]] inline std::uint64_t equality_key_part(entt::entity value) noexcept {
    return static_cast<std::uint64_t>(entt::to_integral(value));
}
template <typename Enum>
    requires std::is_enum_v<Enum>
[[nodiscard]] std::uint64_t equality_key_part(Enum value) noexcept {
    return static_cast<std::uint64_t>(static_cast<std::underlying_type_t<Enum>>(value));
}
// The int a float compared with an int member stands for, when it is integral and fits.
template <std::floating_point Float>
[[nodiscard]] std::optional<int> int_valued(Float value) noexcept {
    const bool integral = std::isfinite(value) && std::trunc(value) == value &&
                          value >= static_cast<Float>(-2147483648.0) && value < static_cast<Float>(2147483648.0);
    return integral ? std::optional<int>{static_cast<int>(value)} : std::nullopt;
}

// A float that is not int-valued gets a key no int, bool or entity part can have.
template <std::floating_point Float>
[[nodiscard]] std::uint64_t equality_key_part(Float value) noexcept {
    constexpr std::uint64_t kNoIntKey = std::uint64_t{1} << 63U;
    const std::optional<int> as_int   = int_valued(value);
    return as_int.has_value() ? equality_key_part(*as_int) : kNoIntKey;
}

// A dense-key part for one value: ints as themselves, bools as 0 or 1, enums
// by their underlying value.
[[nodiscard]] inline std::int64_t dense_key_part(int value) noexcept {
    return value;
}
[[nodiscard]] inline std::int64_t dense_key_part(bool value) noexcept {
    return value ? 1 : 0;
}
template <typename Enum>
    requires std::is_enum_v<Enum>
[[nodiscard]] std::int64_t dense_key_part(Enum value) noexcept {
    return static_cast<std::int64_t>(static_cast<std::underlying_type_t<Enum>>(value));
}
// Lies outside every range, so a query with it finds no row.
inline constexpr std::int64_t kNoDenseKey = std::numeric_limits<std::int64_t>::min();
template <std::floating_point Float>
[[nodiscard]] std::int64_t dense_key_part(Float value) noexcept {
    const std::optional<int> as_int = int_valued(value);
    return as_int.has_value() ? std::int64_t{*as_int} : kNoDenseKey;
}

}  // namespace cactus::runtime::entt_backend
