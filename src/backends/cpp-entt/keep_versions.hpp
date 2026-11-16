#pragma once

#include "backends/cpp-entt/creation_order.hpp"
#include "backends/cpp-entt/perf_counters.hpp"
#include "backends/cpp-entt/relation_store.hpp"

#include <entt/entt.hpp>

#include <algorithm>
#include <any>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

// Entities whose inputs changed since a delta keep's last run.
class KeepDirtySet {
public:
    void mark(entt::entity entity) {
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        if (index >= marked_.size()) {
            marked_.resize(index + 1, entt::null);
        }
        if (marked_[index] == entity) {
            return;
        }
        marked_[index] = entity;
        entities_.push_back(entity);
    }

    void mark_all() noexcept {
        all_ = true;
    }

    // The marked entities, unordered, leaving the set empty; nullopt when any entity may have changed.
    [[nodiscard]] std::optional<std::vector<entt::entity>> take() {
        for (const auto entity : entities_) {
            marked_[static_cast<std::size_t>(entt::to_entity(entity))] = entt::null;
        }
        std::optional<std::vector<entt::entity>> taken;
        if (!all_) {
            taken = std::move(entities_);
        }
        entities_.clear();
        all_ = false;
        return taken;
    }

private:
    std::vector<entt::entity> entities_;
    std::vector<entt::entity> marked_;
    bool all_ = false;
};

// Versions of what keep rules read, one set per registry. A version is a
// value of the clock relation stores take their revisions from, so a keep
// compares every input with one stamp: the clock at the start of its last run.
class KeepVersions {
public:
    KeepVersions(std::size_t slots, std::size_t keeps)
        : versions_(slots),
          subscribers_(slots),
          stamps_(keeps, 0),
          outputs_(keeps, 0),
          dirty_(keeps),
          lock_(std::make_unique<std::mutex>()) {}
    // Registry context storage copies a copyable value; the atomics can't be copied.
    KeepVersions(const KeepVersions&)            = delete;
    KeepVersions& operator=(const KeepVersions&) = delete;
    KeepVersions(KeepVersions&&)                 = default;
    KeepVersions& operator=(KeepVersions&&)      = default;
    ~KeepVersions()                              = default;

    // A delta keep that reads `slot` learns which entities changed it.
    void subscribe(std::size_t slot, std::uint32_t keep) {
        subscribers_[slot].push_back(keep);
    }

    // Parallel handlers may touch the same slot, so a version is atomic and dirty sets are locked.
    // Without an entity, every entity may have changed.
    void touch(std::size_t slot, std::optional<entt::entity> entity = std::nullopt) {
        versions_[slot].store(next_relation_revision(), std::memory_order_relaxed);
        if (subscribers_[slot].empty()) {
            return;
        }
        const std::scoped_lock guard(*lock_);
        for (const auto keep : subscribers_[slot]) {
            if (entity.has_value()) {
                dirty_[keep].mark(*entity);
            } else {
                dirty_[keep].mark_all();
            }
        }
    }

    // The entities whose inputs changed for `keep`, destroyed ones too; nullopt when any may have.
    [[nodiscard]] std::optional<std::vector<entt::entity>> take_dirty(std::size_t keep) {
        return dirty_[keep].take();
    }

    // The clock when `keep` last started a run, 0 before the first.
    [[nodiscard]] std::uint64_t last_run(std::size_t keep) const noexcept {
        return stamps_[keep];
    }

    // After every input version is read: a relation dates a change when its revision is first read.
    void start_run(std::size_t keep) noexcept {
        stamps_[keep] = next_relation_revision();
    }

    [[nodiscard]] std::uint64_t version(std::size_t slot) const noexcept {
        return versions_[slot].load(std::memory_order_relaxed);
    }

    // The version of what `keep` keeps right after its own commit command applied.
    void record_output(std::size_t keep, std::uint64_t version) noexcept {
        outputs_[keep] = version;
    }

    // Whether something other than `keep`'s own commands changed what it keeps.
    [[nodiscard]] bool output_changed(std::size_t keep, std::uint64_t version) const noexcept {
        return version != outputs_[keep];
    }

    [[nodiscard]] bool changed_since(std::uint64_t stamp, std::span<const std::uint32_t> slots) const noexcept {
        return stamp == 0 || std::ranges::any_of(slots, [&](std::uint32_t slot) {
                   return versions_[slot].load(std::memory_order_relaxed) > stamp;
               });
    }

private:
    std::vector<std::atomic<std::uint64_t>> versions_;
    std::vector<std::vector<std::uint32_t>> subscribers_;
    std::vector<std::uint64_t> stamps_;
    std::vector<std::uint64_t> outputs_;
    std::vector<KeepDirtySet> dirty_;
    std::unique_ptr<std::mutex> lock_;
};

// The live entities of `entities`, oldest first and each once.
[[nodiscard]] inline std::vector<entt::entity> live_oldest_first(const entt::registry& registry,
                                                                 std::vector<entt::entity> entities) {
    std::erase_if(entities, [&](entt::entity entity) { return !registry.valid(entity); });
    sort_unique_in_creation_order(registry, entities);
    return entities;
}

// A run that re-evaluates this many units costs about as much as a whole run.
[[nodiscard]] inline bool keep_delta_too_large(std::size_t units, std::size_t domain) noexcept {
    return units * 4 > domain;
}

// About how many entities `view` holds: exact for one trait, an upper bound for several.
template <typename View>
[[nodiscard]] std::size_t view_size_hint(const View& view) {
    if constexpr (requires { view.size_hint(); }) {
        return view.size_hint();
    } else {
        return view.size();
    }
}

// Which source rows witness which kept rows of a row-domain relation keep.
class KeepWitnesses {
public:
    using Key = std::uint64_t;

    [[nodiscard]] static Key key(entt::entity from, entt::entity to) noexcept {
        return (Key{entt::to_integral(from)} << 32U) | Key{entt::to_integral(to)};
    }

    [[nodiscard]] static std::pair<entt::entity, entt::entity> ends(Key key) noexcept {
        return {entt::entity{static_cast<entt::id_type>(key >> 32U)},
                entt::entity{static_cast<entt::id_type>(key & 0xFFFFFFFFU)}};
    }

    // Source row `source` now witnesses `kept`, or nothing.
    void move(Key source, std::optional<Key> kept) {
        const auto found = produced_.find(source);
        const auto old   = found == produced_.end() ? std::nullopt : std::optional<Key>{found->second};
        if (old == kept) {
            return;
        }
        if (old.has_value()) {
            note(*old);
            if (--counts_[*old] == 0) {
                counts_.erase(*old);
            }
            produced_.erase(found);
        }
        if (kept.has_value()) {
            note(*kept);
            ++counts_[*kept];
            produced_[source] = *kept;
        }
    }

    // A full run's witness after `clear`: unlike `move`, it reports no change.
    void add(Key source, Key kept) {
        ++counts_[kept];
        produced_[source] = kept;
    }

    [[nodiscard]] bool witnessed(Key kept) const {
        return counts_.contains(kept);
    }

    // Kept rows whose witnessed state the moves since the last call changed, with their state before them.
    [[nodiscard]] std::vector<std::pair<Key, bool>> take_touched() {
        touched_order_.clear();
        return std::exchange(touched_, {});
    }

    void clear() {
        produced_.clear();
        counts_.clear();
        touched_.clear();
        touched_order_.clear();
    }

private:
    void note(Key kept) {
        if (touched_order_.insert(kept).second) {
            touched_.emplace_back(kept, counts_.contains(kept));
        }
    }

    std::unordered_map<Key, Key> produced_;
    std::unordered_map<Key, std::uint32_t> counts_;
    std::vector<std::pair<Key, bool>> touched_;
    std::unordered_set<Key> touched_order_;
};

// Full run: a row the body appended to `kept` past `before` gains `source` as a witness.
template <typename Source, typename Entry>
void witness_kept_row(KeepWitnesses& witnesses, const Source& source, const std::vector<Entry>& kept, std::size_t before) {
    if (kept.size() > before) {
        witnesses.add(KeepWitnesses::key(source.from, source.to), KeepWitnesses::key(kept.back().from, kept.back().to));
    }
}

// A row-domain keep's state between runs.
struct KeepRowState {
    KeepWitnesses witnesses;
    std::optional<std::size_t> reader;  // the source relation's journal
    bool built = false;
};

// What a row-domain keep's delta run changes.
template <typename Entry>
struct KeepRowChanges {
    std::vector<Entry> added;
    std::vector<std::pair<entt::entity, entt::entity>> removed;
};

// Re-evaluates the source rows in `changed`, oldest row first, through
// `passes` and `body` (which appends to `kept`), and moves their witnesses.
template <typename Entry, typename Store, typename Passes, typename Body>
[[nodiscard]] KeepRowChanges<Entry> keep_row_delta(const Store& rows,
                                                   KeepWitnesses& witnesses,
                                                   std::vector<std::pair<entt::entity, entt::entity>> changed,
                                                   const Passes& passes,
                                                   const Body& body,
                                                   std::vector<Entry>& kept) {
    std::ranges::sort(changed);
    const auto repeats = std::ranges::unique(changed);
    changed.erase(repeats.begin(), repeats.end());
    using Source = std::remove_cvref_t<decltype(*rows.find_entry(entt::entity{}, entt::entity{}))>;
    struct Changed {
        std::size_t position = 0;  // 0 for a row that is gone
        entt::entity from   = entt::null;
        entt::entity to     = entt::null;
        const Source* entry = nullptr;
    };
    std::vector<Changed> ordered;
    ordered.reserve(changed.size());
    for (const auto& [from, to] : changed) {
        const auto* entry = rows.find_entry(from, to);
        ordered.push_back({.position = entry == nullptr ? std::size_t{0} : rows.order_of(*entry) + 1,
                           .from     = from,
                           .to       = to,
                           .entry    = entry});
    }
    std::ranges::stable_sort(ordered, {}, &Changed::position);
    std::unordered_map<KeepWitnesses::Key, Entry> first_kept;
    std::vector<KeepWitnesses::Key> kept_order;
    for (const auto& [position, from, to, entry] : ordered) {
        std::optional<KeepWitnesses::Key> witnessed;
        if (entry != nullptr && passes(*entry)) {
            const auto before = kept.size();
            body(*entry);
            if (kept.size() > before) {
                const auto& row = kept.back();
                witnessed       = KeepWitnesses::key(row.from, row.to);
                if (first_kept.emplace(*witnessed, row).second) {
                    kept_order.push_back(*witnessed);
                }
            }
        }
        witnesses.move(KeepWitnesses::key(from, to), witnessed);
    }
    KeepRowChanges<Entry> changes;
    std::unordered_set<KeepWitnesses::Key> gained;
    for (const auto& [row, before] : witnesses.take_touched()) {
        const bool now = witnesses.witnessed(row);
        if (before && !now) {
            changes.removed.push_back(KeepWitnesses::ends(row));
        }
        if (!before && now) {
            gained.insert(row);
        }
    }
    for (const auto row : kept_order) {
        if (gained.contains(row)) {
            changes.added.push_back(first_kept.at(row));
        }
    }
    return changes;
}

// The equality key of one binding of a pair keep, per entity.
template <typename Key>
class KeepKeyIndex {
public:
    struct Hash {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
            std::size_t seed = 0;
            std::apply(
                [&](const auto&... part) {
                    ((seed ^= std::hash<std::decay_t<decltype(part)>>{}(part) + 0x9E3779B97F4A7C15ULL + (seed << 6U) +
                              (seed >> 2U)),
                     ...);
                },
                key);
            return seed;
        }
    };

    // Sets `entity`'s key, or forgets the entity when `key` is empty; returns its previous key.
    std::optional<Key> assign(entt::entity entity, const std::optional<Key>& key) {
        std::optional<Key> previous;
        if (const auto found = keys_.find(entity); found != keys_.end()) {
            previous     = found->second;
            auto& bucket = members_[found->second];
            std::erase(bucket, entity);
            if (bucket.empty()) {
                members_.erase(found->second);
            }
            keys_.erase(found);
        }
        if (key.has_value()) {
            keys_.emplace(entity, *key);
            members_[*key].push_back(entity);
        }
        return previous;
    }

    template <typename Visit>
    void for_each(const Key& key, const Visit& visit) const {
        if (const auto found = members_.find(key); found != members_.end()) {
            for (const auto entity : found->second) {
                visit(entity);
            }
        }
    }

    void clear() noexcept {
        members_.clear();
        keys_.clear();
    }

private:
    std::unordered_map<Key, std::vector<entt::entity>, Hash> members_;
    std::unordered_map<entt::entity, Key> keys_;
};

// The keys of both bindings of a delta pair keep: its units' and the other binding's.
template <typename Key>
struct KeepPairIndexes {
    KeepKeyIndex<Key> units;
    KeepKeyIndex<Key> others;
    bool built = false;
};

template <std::size_t Keep, typename State>
struct KeepStateSlot {
    State state;
};

// State one keep rule carries between runs, per registry.
template <std::size_t Keep, typename State>
State& keep_state(entt::registry& registry) {
    if (auto* slot = registry.ctx().find<KeepStateSlot<Keep, State>>()) {
        return slot->state;
    }
    return registry.ctx().emplace<KeepStateSlot<Keep, State>>().state;
}

// Rebuilds both key indexes from whole snapshots.
template <typename Key, typename UnitKey, typename OtherKey>
void rebuild_keep_pair_indexes(KeepPairIndexes<Key>& indexes,
                               std::span<const entt::entity> units,
                               std::span<const entt::entity> others,
                               const UnitKey& unit_key,
                               const OtherKey& other_key) {
    indexes.units.clear();
    indexes.others.clear();
    for (const auto unit : units) {
        (void)indexes.units.assign(unit, unit_key(unit));
    }
    for (const auto other : others) {
        (void)indexes.others.assign(other, other_key(other));
    }
    indexes.built = true;
}

// The units whose rows may differ: changed units, and the units an entity of
// the other binding matched before or matches now. Live ones, oldest first.
template <typename Key, typename InUnit, typename UnitKey, typename InOther, typename OtherKey>
[[nodiscard]] std::vector<entt::entity> keep_pair_units(const entt::registry& registry,
                                                        KeepPairIndexes<Key>& indexes,
                                                        std::span<const entt::entity> dirty,
                                                        const InUnit& in_unit,
                                                        const UnitKey& unit_key,
                                                        const InOther& in_other,
                                                        const OtherKey& other_key) {
    std::vector<entt::entity> units;
    for (const auto entity : dirty) {
        const bool member = in_unit(entity);
        const auto old    = indexes.units.assign(entity, member ? std::optional<Key>{unit_key(entity)} : std::nullopt);
        if (member || old.has_value()) {
            units.push_back(entity);
        }
    }
    const auto add_partners = [&](const Key& key) {
        indexes.units.for_each(key, [&](entt::entity unit) { units.push_back(unit); });
    };
    for (const auto entity : dirty) {
        const auto key = in_other(entity) ? std::optional<Key>{other_key(entity)} : std::nullopt;
        if (const auto old = indexes.others.assign(entity, key); old.has_value()) {
            add_partners(*old);
        }
        if (key.has_value()) {
            add_partners(*key);
        }
    }
    return live_oldest_first(registry, std::move(units));
}

// The other binding's entities that match some of `units`, oldest first.
template <typename Key, typename UnitKey>
[[nodiscard]] std::vector<entt::entity> keep_pair_partners(const entt::registry& registry,
                                                           const KeepPairIndexes<Key>& indexes,
                                                           std::span<const entt::entity> units,
                                                           const UnitKey& unit_key) {
    std::vector<entt::entity> partners;
    for (const auto unit : units) {
        indexes.others.for_each(unit_key(unit), [&](entt::entity other) { partners.push_back(other); });
    }
    sort_unique_in_creation_order(registry, partners);
    return partners;
}

// The entities of `units` that `member` accepts, in `units`' order.
template <typename Member>
[[nodiscard]] std::vector<entt::entity> keep_units_where(std::span<const entt::entity> units, const Member& member) {
    std::vector<entt::entity> kept;
    std::ranges::copy_if(units, std::back_inserter(kept), member);
    return kept;
}

// The carriers of `Trait` among `units` this run doesn't keep, in `units`' order.
template <typename Trait, typename Kept>
[[nodiscard]] std::vector<entt::entity> leaving_carriers_among(const entt::registry& registry,
                                                              const Kept& kept,
                                                              std::span<const entt::entity> units) {
    std::vector<entt::entity> leaving;
    for (const auto unit : units) {
        if (registry.all_of<Trait>(unit) && !kept(unit)) {
            leaving.push_back(unit);
        }
    }
    return leaving;
}

// Touches `slots` when a handler returns, after every write it made.
class KeepTouchOnExit {
public:
    KeepTouchOnExit(KeepVersions& versions, std::span<const std::uint32_t> slots) noexcept
        : versions_(&versions), slots_(slots) {}
    KeepTouchOnExit(const KeepTouchOnExit&)            = delete;
    KeepTouchOnExit& operator=(const KeepTouchOnExit&) = delete;
    KeepTouchOnExit(KeepTouchOnExit&&)                 = delete;
    KeepTouchOnExit& operator=(KeepTouchOnExit&&)      = delete;
    // A lost touch would leave a keep stale, so a failure here ends the program.
    ~KeepTouchOnExit() {
        try {
            for (const auto slot : slots_) {
                versions_->touch(slot);
            }
        } catch (...) {
            std::terminate();
        }
    }

private:
    KeepVersions* versions_;
    std::span<const std::uint32_t> slots_;
};

// Whether relation `Row` changed after `stamp`.
template <typename Row>
[[nodiscard]] bool relation_changed_since(entt::registry& registry, std::uint64_t stamp) {
    return uncompacted_relation_store<Row>(registry).revision() > stamp;
}

struct KeepRunStart {
    bool unchanged = false;  // no input changed since the last run
    bool whole     = false;  // first run, or something else changed what it keeps
    std::optional<std::vector<entt::entity>> dirty;  // a delta keep's changed entities; nullopt for a whole run
};

// Compares a keep's inputs with its last run and, unless this is a check run, starts the next one.
template <typename... Relations, typename Output>
[[nodiscard]] KeepRunStart start_keep_run(entt::registry& registry,
                                          KeepVersions& versions,
                                          std::size_t keep,
                                          std::span<const std::uint32_t> inputs,
                                          const Output& output,
                                          bool delta,
                                          bool full_check) {
    const auto stamp = versions.last_run(keep);
    bool moved       = versions.changed_since(stamp, inputs);
    ((moved = relation_changed_since<Relations>(registry, stamp) || moved), ...);
    const auto kept = output();
    KeepRunStart start;
    start.whole     = stamp == 0 || versions.output_changed(keep, kept);
    start.unchanged = !full_check && !moved && !start.whole;
    if (full_check) {
        return start;
    }
    versions.record_output(keep, kept);
    versions.start_run(keep);
    if (start.unchanged) {
        return start;
    }
    count_keep_run();
    if (delta) {
        start.dirty = versions.take_dirty(keep);
    }
    if (start.whole) {
        start.dirty.reset();
    }
    return start;
}

// ── Check mode ─────────────────────────────────────────────────────────────
// A skipped keep run is checked by evaluating it fully: the world must
// already hold exactly what that run would keep.

// Compiled in with CACTUS_KEEP_CHECK; a timing test turns it off to measure skipped runs.
[[nodiscard]] inline bool& keep_check_enabled() noexcept {
    static bool enabled = true;
    return enabled;
}

class KeepCheckFailure : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

template <typename Value>
[[nodiscard]] bool same_kept_value(const Value& left, const Value& right) {
    if constexpr (std::equality_comparable<Value>) {
        return left == right;
    } else if constexpr (std::is_trivially_copyable_v<Value>) {
        return std::memcmp(&left, &right, sizeof(Value)) == 0;
    } else {
        return true;
    }
}

[[noreturn]] inline void fail_keep_check(std::string_view rule, std::string_view row, std::string_view problem) {
    throw KeepCheckFailure("keep check: rule " + std::string(rule) + ": row " + std::string(row) + " " +
                           std::string(problem));
}

// A delta run in check mode first runs the keep again in full; that run only records its result here.
[[nodiscard]] inline bool& keep_full_check_flag() noexcept {
    thread_local bool active = false;
    return active;
}

[[nodiscard]] inline bool keep_full_check_active() noexcept {
    return keep_full_check_flag();
}

class KeepFullCheck {
public:
    KeepFullCheck() noexcept : previous_(std::exchange(keep_full_check_flag(), true)) {}
    KeepFullCheck(const KeepFullCheck&)            = delete;
    KeepFullCheck& operator=(const KeepFullCheck&) = delete;
    KeepFullCheck(KeepFullCheck&&)                 = delete;
    KeepFullCheck& operator=(KeepFullCheck&&)      = delete;
    ~KeepFullCheck() { keep_full_check_flag() = previous_; }

private:
    bool previous_;
};

[[nodiscard]] inline std::any& keep_check_expected() noexcept {
    thread_local std::any expected;
    return expected;
}

[[nodiscard]] inline std::string keep_row_name(entt::entity entity) {
    return "(" + std::to_string(entt::to_integral(entity)) + ")";
}

[[nodiscard]] inline std::string keep_row_name(entt::entity from, entt::entity to) {
    return "(" + std::to_string(entt::to_integral(from)) + ", " + std::to_string(entt::to_integral(to)) + ")";
}

// A trait keep's full result: `kept` carriers and no `leaving` ones. When the
// keep sets fields, `same` compares the fields it sets.
template <typename Trait, typename Kept, typename Same>
void check_kept_unchanged(const entt::registry& registry,
                          const std::vector<Kept>& kept,
                          std::span<const entt::entity> leaving,
                          std::string_view rule,
                          const Same& same) {
    if (!leaving.empty()) {
        fail_keep_check(rule, keep_row_name(leaving.front()), "is still kept after a skipped run");
    }
    for (const auto& target : kept) {
        if constexpr (std::is_same_v<Kept, entt::entity>) {
            if (!registry.all_of<Trait>(target)) {
                fail_keep_check(rule, keep_row_name(target), "is missing after a skipped run");
            }
        } else {
            const auto* current = registry.try_get<Trait>(target.first);
            if (current == nullptr) {
                fail_keep_check(rule, keep_row_name(target.first), "is missing after a skipped run");
            }
            if (!same(*current, target.second)) {
                fail_keep_check(rule, keep_row_name(target.first), "has stale values after a skipped run");
            }
        }
    }
}

// What a full run of a trait keep keeps, as the delta check compares it.
template <typename Kept>
struct KeptTraitResult {
    std::vector<Kept> kept;
    std::vector<entt::entity> leaving;
};

[[nodiscard]] inline entt::entity kept_target(entt::entity target) noexcept {
    return target;
}

template <typename Trait>
[[nodiscard]] entt::entity kept_target(const std::pair<entt::entity, Trait>& target) noexcept {
    return target.first;
}

// A trait keep's delta run over `units` against its full run: the same
// targets and values among the units, and the world already right elsewhere.
template <typename Trait, typename Kept, typename Same>
void check_kept_delta(const entt::registry& registry,
                      const KeptTraitResult<Kept>& full,
                      const KeptTraitResult<Kept>& delta,
                      std::span<const entt::entity> units,
                      std::string_view rule,
                      const Same& same) {
    const std::unordered_set<entt::entity> unit_set(units.begin(), units.end());
    const auto of_units = [&](const Kept& target) { return unit_set.contains(kept_target(target)); };
    std::vector<Kept> outside;
    std::ranges::copy_if(full.kept, std::back_inserter(outside), std::not_fn(of_units));
    check_kept_unchanged<Trait>(registry, outside, {}, rule, same);
    std::vector<Kept> full_units;
    std::vector<Kept> delta_units;
    std::ranges::copy_if(full.kept, std::back_inserter(full_units), of_units);
    std::ranges::copy_if(delta.kept, std::back_inserter(delta_units), of_units);
    const auto equal = [&](const Kept& left, const Kept& right) {
        if constexpr (std::is_same_v<Kept, entt::entity>) {
            return left == right;
        } else {
            return left.first == right.first && same(left.second, right.second);
        }
    };
    const auto mismatch = std::ranges::mismatch(full_units, delta_units, equal);
    if (mismatch.in1 != full_units.end()) {
        fail_keep_check(rule, keep_row_name(kept_target(*mismatch.in1)), "differs after a delta run");
    }
    if (mismatch.in2 != delta_units.end()) {
        fail_keep_check(rule, keep_row_name(kept_target(*mismatch.in2)), "is extra after a delta run");
    }
    const auto missing = [&](const std::vector<entt::entity>& from, const std::vector<entt::entity>& in) {
        return std::ranges::find_if(from, [&](entt::entity entity) { return !std::ranges::contains(in, entity); });
    };
    if (const auto stale = missing(full.leaving, delta.leaving); stale != full.leaving.end()) {
        fail_keep_check(rule, keep_row_name(*stale), "is still kept after a delta run");
    }
    if (const auto dropped = missing(delta.leaving, full.leaving); dropped != delta.leaving.end()) {
        fail_keep_check(rule, keep_row_name(*dropped), "is dropped by a delta run");
    }
}

// Outside the units of a delta run, the store must already hold exactly what the full run keeps.
// `when` names the run in a failure.
template <typename Entry>
void check_relation_rows_outside(entt::registry& registry,
                                 const std::vector<Entry>& full,
                                 const std::unordered_set<entt::entity>& units,
                                 bool from_end,
                                 std::string_view rule,
                                 std::string_view when = "outside a delta run") {
    using Row          = decltype(Entry::row);
    const auto& store  = relation_store<Row>(registry);
    const auto problem = [&](std::string_view what) { return std::string(what) + " " + std::string(when); };
    std::unordered_set<KeepWitnesses::Key> selected;
    for (const auto& entry : full) {
        selected.insert(KeepWitnesses::key(entry.from, entry.to));
        if (units.contains(from_end ? entry.from : entry.to) || !valid_relation_ends<Row>(registry, entry.from, entry.to)) {
            continue;
        }
        const auto* current = store.find(entry.from, entry.to);
        if (current == nullptr) {
            fail_keep_check(rule, keep_row_name(entry.from, entry.to), problem("is missing"));
        }
        if (!same_kept_value(*current, entry.row)) {
            fail_keep_check(rule, keep_row_name(entry.from, entry.to), problem("has stale values"));
        }
    }
    for (const auto& entry : store.rows()) {
        if (!units.contains(from_end ? entry.from : entry.to) &&
            !selected.contains(KeepWitnesses::key(entry.from, entry.to))) {
            fail_keep_check(rule, keep_row_name(entry.from, entry.to), problem("is still kept"));
        }
    }
}

// A relation keep's full result: exactly `rows`, with their values.
template <typename Entry>
void check_relation_keep_unchanged(entt::registry& registry, const std::vector<Entry>& rows, std::string_view rule) {
    check_relation_rows_outside(registry, rows, {}, true, rule, "after a skipped run");
}

// A relation keep's delta run over the rows of `units` against its full run.
template <typename Entry>
void check_relation_delta(entt::registry& registry,
                          const std::vector<Entry>& full,
                          const std::vector<Entry>& delta,
                          std::span<const entt::entity> units,
                          bool from_end,
                          std::string_view rule) {
    using Row = decltype(Entry::row);
    const std::unordered_set<entt::entity> unit_set(units.begin(), units.end());
    check_relation_rows_outside(registry, full, unit_set, from_end, rule);
    const auto of_units = [&](const Entry& entry) {
        return unit_set.contains(from_end ? entry.from : entry.to) &&
               valid_relation_ends<Row>(registry, entry.from, entry.to);
    };
    std::vector<Entry> full_units;
    std::vector<Entry> delta_units;
    std::ranges::copy_if(full, std::back_inserter(full_units), of_units);
    std::ranges::copy_if(delta, std::back_inserter(delta_units), of_units);
    const auto same = [](const Entry& left, const Entry& right) {
        return left.from == right.from && left.to == right.to && same_kept_value(left.row, right.row);
    };
    const auto mismatch = std::ranges::mismatch(full_units, delta_units, same);
    if (mismatch.in1 != full_units.end()) {
        fail_keep_check(rule, keep_row_name(mismatch.in1->from, mismatch.in1->to), "differs after a delta run");
    }
    if (mismatch.in2 != delta_units.end()) {
        fail_keep_check(rule, keep_row_name(mismatch.in2->from, mismatch.in2->to), "is extra after a delta run");
    }
}

// The rows a delta run adds come in the order the full run adds them.
template <typename Store, typename Entry>
void check_new_row_order(const Store& store,
                         const std::vector<Entry>& full_new,
                         std::vector<Entry> added,
                         const std::unordered_set<KeepWitnesses::Key>& removed,
                         std::string_view rule) {
    std::erase_if(added, [&](const Entry& entry) {
        return store.find(entry.from, entry.to) != nullptr &&
               !removed.contains(KeepWitnesses::key(entry.from, entry.to));
    });
    const auto same_ends = [](const Entry& left, const Entry& right) {
        return left.from == right.from && left.to == right.to;
    };
    const auto mismatch = std::ranges::mismatch(full_new, added, same_ends);
    if (mismatch.in1 != full_new.end()) {
        fail_keep_check(rule, keep_row_name(mismatch.in1->from, mismatch.in1->to), "is added out of order by a delta run");
    }
    if (mismatch.in2 != added.end()) {
        fail_keep_check(rule, keep_row_name(mismatch.in2->from, mismatch.in2->to), "is added only by a delta run");
    }
}

// A row-domain keep's delta changes against its full run: the store after
// the changes holds the full run's rows, and new rows come in its order.
template <typename Entry>
void check_relation_changes(entt::registry& registry,
                            const std::vector<Entry>& full,
                            const KeepRowChanges<Entry>& changes,
                            std::string_view rule) {
    using Row         = decltype(Entry::row);
    const auto& store = relation_store<Row>(registry);
    const auto valid  = [&](const Entry& entry) { return valid_relation_ends<Row>(registry, entry.from, entry.to); };
    std::unordered_set<KeepWitnesses::Key> removed;
    for (const auto& [from, to] : changes.removed) {
        removed.insert(KeepWitnesses::key(from, to));
    }
    std::unordered_map<KeepWitnesses::Key, Row> predicted;
    for (const auto& entry : store.rows()) {
        if (!removed.contains(KeepWitnesses::key(entry.from, entry.to))) {
            predicted.emplace(KeepWitnesses::key(entry.from, entry.to), entry.row);
        }
    }
    std::vector<Entry> added;
    std::ranges::copy_if(changes.added, std::back_inserter(added), valid);
    for (const auto& entry : added) {
        predicted.insert_or_assign(KeepWitnesses::key(entry.from, entry.to), entry.row);
    }
    std::vector<Entry> full_new;
    std::unordered_set<KeepWitnesses::Key> selected;
    for (const auto& entry : full) {
        const auto key = KeepWitnesses::key(entry.from, entry.to);
        if (!valid(entry) || !selected.insert(key).second) {
            continue;
        }
        const auto found = predicted.find(key);
        if (found == predicted.end()) {
            fail_keep_check(rule, keep_row_name(entry.from, entry.to), "is missing after a delta run");
        }
        if (!same_kept_value(found->second, entry.row)) {
            fail_keep_check(rule, keep_row_name(entry.from, entry.to), "has other values after a delta run");
        }
        if (store.find(entry.from, entry.to) == nullptr || removed.contains(key)) {
            full_new.push_back(entry);
        }
    }
    for (const auto& [key, row] : predicted) {
        if (!selected.contains(key)) {
            const auto [from, to] = KeepWitnesses::ends(key);
            fail_keep_check(rule, keep_row_name(from, to), "is still kept after a delta run");
        }
    }
    check_new_row_order(store, full_new, std::move(added), removed, rule);
}

}  // namespace cactus::runtime::entt_backend
