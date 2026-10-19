#pragma once

#include "backends/cpp-entt/creation_order.hpp"

#include <entt/entt.hpp>
#include <raylib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
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

// Which world an index was built for, and the entities whose membership may
// have changed since it last caught up.
class BindingIndexTracker {
public:
    [[nodiscard]] bool stale(const entt::registry& registry) const noexcept {
        return registry_ != &registry || epoch_ != binding_index_epoch();
    }

    void reset(const entt::registry& registry) {
        registry_ = &registry;
        epoch_    = binding_index_epoch();
        pending_.clear();
    }

    // Signal listener for on_construct / on_destroy of a watched trait.
    void on_signal(entt::registry& /*registry*/, entt::entity entity) { pending_.push_back(entity); }

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

private:
    const entt::registry* registry_ = nullptr;
    std::uint64_t epoch_            = 0;
    std::vector<entt::entity> pending_;
    std::vector<entt::entity> taken_;
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

// Axis-aligned 3D bounds of static members in a uniform grid. Bounds that are
// not finite, or that cover too many cells, are kept aside and offered to
// every query.
class StaticProxyGrid3D {
public:
    void clear(float cell_size);
    void insert(entt::entity entity, std::uint64_t ordinal, Vector3 min, Vector3 max);
    void remove(entt::entity entity);
    // Appends every member whose bounds overlap [min, max], each once, oldest first.
    void query(Vector3 min, Vector3 max, std::vector<OrderedEntity>& out) const;
    [[nodiscard]] bool contains(entt::entity entity) const noexcept { return members_.contains(entity); }
    [[nodiscard]] std::size_t size() const noexcept { return members_.size(); }

private:
    struct CellKey {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        friend bool operator==(const CellKey&, const CellKey&) = default;
    };
    struct CellKeyHash {
        std::size_t operator()(const CellKey& key) const noexcept;
    };
    struct Member {
        std::uint64_t ordinal;
        Vector3 min;
        Vector3 max;
        bool aside;
    };

    [[nodiscard]] std::optional<std::array<CellKey, 2>> cell_range(Vector3 min, Vector3 max) const noexcept;
    void offer(entt::entity entity, Vector3 min, Vector3 max, std::vector<OrderedEntity>& out) const;
    void scan_members(Vector3 min, Vector3 max, bool test_overlap, std::vector<OrderedEntity>& out) const;
    void scan_cells(Vector3 min, Vector3 max, std::vector<OrderedEntity>& out) const;
    template <typename Visit>
    static void for_each_cell(const std::array<CellKey, 2>& range, const Visit& visit);

    float cell_size_ = 1.0F;
    std::unordered_map<entt::entity, Member> members_;
    std::unordered_map<CellKey, std::vector<entt::entity>, CellKeyHash> cells_;
    std::vector<entt::entity> aside_;
};

// One pair binding's members, split by whether their bounds can change: static
// members live in a grid, built once; dynamic members are listed in creation
// order and their bounds recomputed by each pass.
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

    void reset(const entt::registry& registry, float cell_size) {
        tracker_.reset(registry);
        grid_.clear(cell_size);
        dynamic_.clear();
    }

    // Reclassifies every marked entity. Classify(entt::entity) -> Classified.
    template <typename Classify>
    void catch_up(const entt::registry& registry, const Classify& classify) {
        for (const auto entity : tracker_.take_pending()) {
            grid_.remove(entity);
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
            if (found.bounds.has_value()) {
                grid_.insert(entity, ordinal, found.bounds->first, found.bounds->second);
            }
        }
    }

    using Bounds        = std::pair<Vector3, Vector3>;
    using BoundedMember = std::pair<OrderedEntity, Bounds>;

    // Static members whose bounds overlap [min, max], oldest first.
    void query_static(Vector3 min, Vector3 max, std::vector<OrderedEntity>& out) const {
        grid_.query(min, max, out);
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
    void candidates(Vector3 min,
                    Vector3 max,
                    const std::vector<BoundedMember>& dynamic,
                    std::vector<OrderedEntity>& out) const {
        out.clear();
        grid_.query(min, max, out);
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
    [[nodiscard]] std::size_t static_size() const noexcept { return grid_.size(); }

private:
    BindingIndexTracker tracker_;
    StaticProxyGrid3D grid_;
    std::vector<OrderedEntity> dynamic_;
};

// A pair binding's members keyed by values that never change after
// construction (`let` fields, the entity itself), each key's members oldest
// first.
template <std::size_t K>
class EqualityBindingIndex {
public:
    using Key = std::array<std::uint64_t, K>;

    [[nodiscard]] BindingIndexTracker& tracker() noexcept { return tracker_; }

    void reset(const entt::registry& registry) {
        tracker_.reset(registry);
        members_.clear();
        buckets_.clear();
    }

    // KeyOf(entt::entity) -> std::optional<Key>, nullopt for a non-member.
    template <typename KeyOf>
    void catch_up(const entt::registry& registry, const KeyOf& key_of) {
        for (const auto entity : tracker_.take_pending()) {
            erase(entity);
            if (!registry.valid(entity)) {
                continue;
            }
            const std::optional<Key> key = key_of(entity);
            if (!key.has_value()) {
                continue;
            }
            const OrderedEntity member{creation_ordinal(registry, entity), entity};
            auto& bucket = buckets_[*key];
            bucket.insert(std::ranges::upper_bound(bucket, member), member);
            members_.emplace(entity, *key);
        }
    }

    // Members with exactly this key, oldest first; empty when there are none.
    [[nodiscard]] const std::vector<OrderedEntity>& find(const Key& key) const {
        static const std::vector<OrderedEntity> none;
        const auto found = buckets_.find(key);
        return found == buckets_.end() ? none : found->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return members_.size(); }

private:
    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const auto part : key) {
                hash = (hash ^ part) * 1099511628211ULL;
                hash ^= hash >> 29U;
            }
            return static_cast<std::size_t>(hash);
        }
    };

    void erase(entt::entity entity) {
        const auto found = members_.find(entity);
        if (found == members_.end()) {
            return;
        }
        auto bucket = buckets_.find(found->second);
        if (bucket != buckets_.end()) {
            std::erase_if(bucket->second, [&](const OrderedEntity& member) { return member.second == entity; });
            if (bucket->second.empty()) {
                buckets_.erase(bucket);
            }
        }
        members_.erase(found);
    }

    BindingIndexTracker tracker_;
    std::unordered_map<entt::entity, Key> members_;
    std::unordered_map<Key, std::vector<OrderedEntity>, KeyHash> buckets_;
};

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
// A float compared with an int member: an integral value shares that int's key,
// any other value gets a key no int, bool or entity part can have.
template <std::floating_point Float>
[[nodiscard]] std::uint64_t equality_key_part(Float value) noexcept {
    constexpr std::uint64_t kNoIntKey = std::uint64_t{1} << 63U;
    const bool integral = std::isfinite(value) && std::trunc(value) == value &&
                          value >= static_cast<Float>(-2147483648.0) && value < static_cast<Float>(2147483648.0);
    return integral ? equality_key_part(static_cast<int>(value)) : kNoIntKey;
}

}  // namespace cactus::runtime::entt_backend
