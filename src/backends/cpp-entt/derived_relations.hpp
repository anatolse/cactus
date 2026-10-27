#pragma once

#include "backends/cpp-entt/creation_order.hpp"
#include "backends/cpp-entt/relation_store.hpp"

#include <entt/entt.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

enum class Derivation : std::uint8_t { Closure, Shortest, Components };

template <typename Row>
concept DerivedRow = requires {
    Row::kDerivation;
    typename Row::Source;
};

template <typename Row>
struct DerivedEntry {
    entt::entity from;
    entt::entity to;
    Row row;
};

template <typename Row>
class ClosureCache;
template <typename Row>
class RouteCache;
template <typename Row>
class ComponentCache;

template <typename Row>
using DerivedRelation =
    std::conditional_t<Row::kDerivation == Derivation::Closure,
                       ClosureCache<Row>,
                       std::conditional_t<Row::kDerivation == Derivation::Shortest, RouteCache<Row>, ComponentCache<Row>>>;

template <DerivedRow Row>
DerivedRelation<Row>& derived_relation(entt::registry& registry);

template <DerivedRow Row>
const RelationStore<Row>& materialized_relation(entt::registry& registry);

// The live rows of `Row`'s relation as a store: its own when stored, materialized when derived.
template <typename Row>
const RelationStore<Row>& source_rows(entt::registry& registry) {
    if constexpr (DerivedRow<Row>) {
        return materialized_relation<Row>(registry);
    } else {
        return relation_store<Row>(registry);
    }
}

namespace derived_detail {

constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

using store_detail::at;
using store_detail::slot;

// Entries oldest-first by the end that is not the key.
template <typename Entry>
void sort_by_other_end(const entt::registry& registry, std::vector<Entry>& rows, bool key_is_from) {
    std::ranges::sort(rows, {}, [&](const Entry& entry) {
        return creation_ordinal(registry, key_is_from ? entry.to : entry.from);
    });
}

}  // namespace derived_detail

// Revision bookkeeping shared by every derived cache: a cache is current
// while its source's revision and, for components, its members are unchanged.
template <typename Row>
class DerivedCacheBase {
public:
    // A fresh value whenever the cache drops its rows.
    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }

    // How many times rows were computed: one per cached key, or per whole rebuild.
    [[nodiscard]] std::size_t builds() const noexcept {
        return builds_;
    }

    void members_changed() noexcept {
        members_changed_ = true;
    }

    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> order_of(const DerivedEntry<Row>& entry) const {
        return {creation_ordinal(*registry_, entry.from), creation_ordinal(*registry_, entry.to)};
    }

protected:
    // True when the cached rows are stale and must be dropped.
    bool sync(entt::registry& registry) {
        registry_          = &registry;
        const auto current = source_rows<typename Row::Source>(registry).revision();
        if (current == source_revision_ && !members_changed_) {
            return false;
        }
        source_revision_ = current;
        members_changed_ = false;
        revision_        = next_relation_revision();
        return true;
    }

    [[nodiscard]] const RelationStore<typename Row::Source>& source() const {
        return source_rows<typename Row::Source>(*registry_);
    }

    void count_build() noexcept {
        ++builds_;
    }

    [[nodiscard]] entt::registry& registry() const noexcept {
        return *registry_;
    }

private:
    entt::registry* registry_      = nullptr;
    std::uint64_t source_revision_ = 0;
    std::uint64_t revision_        = 0;
    std::size_t builds_            = 0;
    bool members_changed_          = false;
};

// Rows of one key: oldest-first by the other end, plus a bitset of the other ends.
template <typename Row>
struct KeyedRows {
    std::vector<DerivedEntry<Row>> rows;
    std::vector<std::uint64_t> bits;

    [[nodiscard]] bool has(entt::entity entity) const noexcept {
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        return index / 64 < bits.size() && ((bits[index / 64] >> (index % 64)) & 1U) != 0;
    }

    void mark(entt::entity entity) {
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        if (index / 64 >= bits.size()) {
            bits.resize((index / 64) + 1, 0);
        }
        bits[index / 64] |= std::uint64_t{1} << (index % 64);
    }
};

// Shared lookup surface of the reach and route caches, keyed by either end.
template <typename Row, typename Cache>
class KeyedDerivedCache : public DerivedCacheBase<Row> {
public:
    using Entry = DerivedEntry<Row>;

    void refresh(entt::registry& registry) {
        if (this->sync(registry)) {
            forward_.clear();
            reverse_.clear();
        }
    }

    static void connect(entt::registry& /*registry*/) {}

    // With both ends fixed the `to` end keys the lookup: games have fewer goals than walkers.
    [[nodiscard]] bool contains(entt::entity from, entt::entity to) {
        return this->registry().valid(from) && this->registry().valid(to) && keyed(to, false).has(from);
    }

    [[nodiscard]] bool has_from(entt::entity from) {
        return !keyed(from, true).rows.empty();
    }

    [[nodiscard]] bool has_to(entt::entity to) {
        return !keyed(to, false).rows.empty();
    }

    template <typename Visit>
    void for_each_from(entt::entity from, const Visit& visit) {
        for (const auto& entry : keyed(from, true).rows) {
            visit(entry);
        }
    }

    template <typename Visit>
    void for_each_to(entt::entity to, const Visit& visit) {
        for (const auto& entry : keyed(to, false).rows) {
            visit(entry);
        }
    }

    // Cached keys, for both directions.
    [[nodiscard]] std::size_t cached_keys() const noexcept {
        return forward_.size() + reverse_.size();
    }

private:
    const KeyedRows<Row>& keyed(entt::entity key, bool forward) {
        auto& keys = forward ? forward_ : reverse_;
        if (const auto found = keys.find(key); found != keys.end()) {
            return found->second;
        }
        auto& rows = keys[key];
        if (this->registry().valid(key)) {
            this->count_build();
            static_cast<Cache*>(this)->compute(key, forward, rows.rows);
            derived_detail::sort_by_other_end(this->registry(), rows.rows, forward);
            for (const auto& entry : rows.rows) {
                rows.mark(forward ? entry.to : entry.from);
            }
        }
        return rows;
    }

    std::unordered_map<entt::entity, KeyedRows<Row>> forward_;
    std::unordered_map<entt::entity, KeyedRows<Row>> reverse_;

    KeyedDerivedCache() = default;
    friend Cache;
};

// `closure(R)`: breadth-first reach per key, over `R`'s links or their reverse.
template <typename Row>
class ClosureCache : public KeyedDerivedCache<Row, ClosureCache<Row>> {
public:
    void compute(entt::entity key, bool forward, std::vector<DerivedEntry<Row>>& rows) const {
        const auto& source = this->source();
        std::vector<std::uint8_t> seen;
        std::vector<entt::entity> frontier{key};
        while (!frontier.empty()) {
            const auto at = frontier.back();
            frontier.pop_back();
            const auto step = [&](entt::entity next) {
                auto& visited = derived_detail::slot(seen, next, std::uint8_t{0});
                if (visited != 0) {
                    return;
                }
                visited = 1;
                frontier.push_back(next);
                rows.push_back(forward ? DerivedEntry<Row>{key, next, Row{}} : DerivedEntry<Row>{next, key, Row{}});
            };
            if (forward) {
                source.for_each_from(at, [&](const auto& entry) { step(entry.to); });
            } else {
                source.for_each_to(at, [&](const auto& entry) { step(entry.from); });
            }
        }
    }
};

// `shortest(R, by = f)`: integer Dijkstra per key, forward from a source or
// backward from a goal, with `next` ties broken by creation order.
template <typename Row>
class RouteCache : public KeyedDerivedCache<Row, RouteCache<Row>> {
public:
    static constexpr std::int64_t kUnreached = std::numeric_limits<std::int64_t>::max();
    static constexpr std::int64_t kMaxCost   = std::numeric_limits<std::int32_t>::max();

    void compute(entt::entity key, bool forward, std::vector<DerivedEntry<Row>>& rows) const {
        const auto search   = distances(key, forward);
        const auto distance = [&](entt::entity entity) { return derived_detail::at(search.distance, entity, kUnreached); };
        const auto add      = [&](entt::entity from, entt::entity to, entt::entity next, std::int64_t total) {
            Row row{};
            row.next     = next;
            row.distance = static_cast<decltype(row.distance)>(total);
            rows.push_back(DerivedEntry<Row>{from, to, row});
        };
        if (forward) {
            for (const auto& [to, next] : first_steps(key, search.distance)) {
                if (to != key) {
                    add(key, to, next, distance(to));
                }
            }
            return;
        }
        // Each reached source's step: its lowest-ordinal link onto a cheapest chain to the goal.
        for (const auto source : search.settled) {
            if (source == key) {
                continue;
            }
            entt::entity best = entt::null;
            const auto total  = distance(source);
            this->source().for_each_from(source, [&](const auto& entry) {
                const auto rest = distance(entry.to);
                if (rest == kUnreached || cost(entry.row) + rest != total) {
                    return;
                }
                if (best == entt::null ||
                    creation_ordinal(this->registry(), entry.to) < creation_ordinal(this->registry(), best)) {
                    best = entry.to;
                }
            });
            add(source, key, best, total);
        }
    }

private:
    struct Search {
        std::vector<std::int64_t> distance;
        std::vector<entt::entity> settled;
    };

    [[nodiscard]] static std::int64_t cost(const Row::Source& row) {
        return std::max<std::int64_t>(0, Row::source_cost(row));
    }

    // Least cost from `key` (forward) or to it (backward), zero rows allowed; chains over the int range are dropped.
    [[nodiscard]] Search distances(entt::entity key, bool forward) const {
        Search search;
        using Item = std::pair<std::int64_t, entt::entity>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
        derived_detail::slot(search.distance, key, kUnreached) = 0;
        queue.emplace(0, key);
        while (!queue.empty()) {
            const auto [at_cost, at] = queue.top();
            queue.pop();
            // Improvements are strict, so only a stale entry carries another cost.
            if (at_cost != derived_detail::at(search.distance, at, kUnreached)) {
                continue;
            }
            search.settled.push_back(at);
            const auto relax = [&](entt::entity next, std::int64_t step) {
                const auto total = at_cost + step;
                auto& best       = derived_detail::slot(search.distance, next, kUnreached);
                if (total > kMaxCost || total >= best) {
                    return;
                }
                best = total;
                queue.emplace(total, next);
            };
            if (forward) {
                this->source().for_each_from(at, [&](const auto& entry) { relax(entry.to, cost(entry.row)); });
            } else {
                this->source().for_each_to(at, [&](const auto& entry) { relax(entry.from, cost(entry.row)); });
            }
        }
        return search;
    }

    // For each reached entity, the lowest-ordinal first step `w` of a cheapest
    // chain: candidates in creation order each claim what their cheapest links still reach.
    [[nodiscard]] std::vector<std::pair<entt::entity, entt::entity>> first_steps(entt::entity key,
                                                                   const std::vector<std::int64_t>& distance) const {
        const auto tight = [&](entt::entity from, const auto& entry) {
            const auto at = derived_detail::at(distance, from, kUnreached);
            return at != kUnreached && at + cost(entry.row) == derived_detail::at(distance, entry.to, kUnreached);
        };
        std::vector<entt::entity> candidates;
        this->source().for_each_from(key, [&](const auto& entry) {
            if (tight(key, entry)) {
                candidates.push_back(entry.to);
            }
        });
        std::ranges::sort(candidates, {}, [&](entt::entity entity) { return creation_ordinal(this->registry(), entity); });
        std::vector<entt::entity> step;
        std::vector<std::pair<entt::entity, entt::entity>> result;
        for (const auto candidate : candidates) {
            std::vector<entt::entity> frontier{candidate};
            while (!frontier.empty()) {
                const auto at = frontier.back();
                frontier.pop_back();
                auto& claimed = derived_detail::slot(step, at, entt::entity{entt::null});
                if (claimed != entt::null) {
                    continue;
                }
                claimed = candidate;
                result.emplace_back(at, candidate);
                this->source().for_each_from(at, [&](const auto& entry) {
                    if (tight(at, entry)) {
                        frontier.push_back(entry.to);
                    }
                });
            }
        }
        return result;
    }
};

// `components(R)`: one union-find over every entity carrying the end trait,
// links taken both ways; each group is named by its oldest member.
template <typename Row>
class ComponentCache : public DerivedCacheBase<Row> {
public:
    using Entry = DerivedEntry<Row>;

    void refresh(entt::registry& registry) {
        if (this->sync(registry)) {
            built_ = false;
        }
    }

    // Members come and go with the end trait, not only with links.
    static void connect(entt::registry& registry) {
        using Member = Row::FromTrait;
        registry.on_construct<Member>().template connect<&ComponentCache::mark>();
        registry.on_destroy<Member>().template connect<&ComponentCache::mark>();
    }

    [[nodiscard]] std::span<const Entry> rows() {
        build();
        return entries_;
    }

    [[nodiscard]] bool contains(entt::entity from, entt::entity to) {
        const auto* entry = member(from);
        return entry != nullptr && entry->to == to;
    }

    [[nodiscard]] bool has_from(entt::entity from) {
        return member(from) != nullptr;
    }

    [[nodiscard]] bool has_to(entt::entity to) {
        return contains(to, to);
    }

    template <typename Visit>
    void for_each_from(entt::entity from, const Visit& visit) {
        if (const auto* entry = member(from)) {
            visit(*entry);
        }
    }

    template <typename Visit>
    void for_each_to(entt::entity to, const Visit& visit) {
        if (!has_to(to)) {
            return;
        }
        const auto group = position(to);
        for (auto index = group_begin_[group]; index < group_begin_[group + 1]; ++index) {
            visit(entries_[group_order_[index]]);
        }
    }

private:
    static void mark(entt::registry& registry, entt::entity /*entity*/) {
        if (auto* cache = registry.ctx().find<ComponentCache>()) {
            cache->members_changed();
        }
    }

    [[nodiscard]] std::uint32_t position(entt::entity entity) const {
        const auto index = derived_detail::at(positions_, entity, derived_detail::kNone);
        return index != derived_detail::kNone && entries_[index].from == entity ? index : derived_detail::kNone;
    }

    const Entry* member(entt::entity entity) {
        build();
        const auto index = position(entity);
        return index == derived_detail::kNone ? nullptr : &entries_[index];
    }

    void build() {
        if (built_) {
            return;
        }
        built_ = true;
        this->count_build();
        const auto members =
            creation_ordered(this->registry(), this->registry().template view<typename Row::FromTrait>());
        const auto count = static_cast<std::uint32_t>(members.size());
        positions_.assign(positions_.size(), derived_detail::kNone);
        for (std::uint32_t index = 0; index < count; ++index) {
            derived_detail::slot(positions_, members[index], derived_detail::kNone) = index;
        }
        std::vector<std::uint32_t> parent(count);
        std::ranges::iota(parent, std::uint32_t{0});
        const auto find = [&](std::uint32_t at) {
            while (parent[at] != at) {
                parent[at] = parent[parent[at]];
                at         = parent[at];
            }
            return at;
        };
        this->source().for_each_live([&](const auto& entry) {
            const auto from = derived_detail::at(positions_, entry.from, derived_detail::kNone);
            const auto to   = derived_detail::at(positions_, entry.to, derived_detail::kNone);
            if (from == derived_detail::kNone || to == derived_detail::kNone) {
                return;
            }
            const auto left               = find(from);
            const auto right              = find(to);
            parent[std::max(left, right)] = std::min(left, right);
        });
        // The lowest position is the oldest member, so every root is its group's oldest.
        entries_.clear();
        group_begin_.assign(static_cast<std::size_t>(count) + 1, 0);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto root = find(index);
            entries_.push_back(Entry{members[index], members[root], Row{}});
            ++group_begin_[root + 1];
        }
        std::partial_sum(group_begin_.begin(), group_begin_.end(), group_begin_.begin());
        group_order_.assign(count, 0);
        auto next = group_begin_;
        for (std::uint32_t index = 0; index < count; ++index) {
            group_order_[next[find(index)]++] = index;
        }
    }

    std::vector<Entry> entries_;
    std::vector<std::uint32_t> positions_;
    std::vector<std::uint32_t> group_begin_;
    std::vector<std::uint32_t> group_order_;
    bool built_ = false;
};

template <DerivedRow Row>
DerivedRelation<Row>& derived_relation(entt::registry& registry) {
    auto* cache = registry.ctx().find<DerivedRelation<Row>>();
    if (cache == nullptr) {
        cache = &registry.ctx().emplace<DerivedRelation<Row>>();
        DerivedRelation<Row>::connect(registry);
    }
    cache->refresh(registry);
    return *cache;
}

namespace derived_detail {

template <typename Row>
struct Materialized {
    RelationStore<Row> store;
    std::uint64_t built_from = 0;
};

}  // namespace derived_detail

// Every row of a derived relation, for a relation derived from it.
template <DerivedRow Row>
const RelationStore<Row>& materialized_relation(entt::registry& registry) {
    auto& derived = derived_relation<Row>(registry);
    auto* cached  = registry.ctx().find<derived_detail::Materialized<Row>>();
    if (cached == nullptr) {
        cached = &registry.ctx().emplace<derived_detail::Materialized<Row>>();
    }
    if (cached->built_from == derived.revision()) {
        return cached->store;
    }
    cached->built_from = derived.revision();
    cached->store.clear();
    cached->store.compact();
    const auto add = [&](const DerivedEntry<Row>& entry) { cached->store.add(entry.from, entry.to, entry.row); };
    if constexpr (Row::kDerivation == Derivation::Components) {
        for (const auto& entry : derived.rows()) {
            add(entry);
        }
    } else {
        for (const auto member : creation_ordered(registry, registry.view<typename Row::FromTrait>())) {
            derived.for_each_from(member, add);
        }
    }
    return cached->store;
}

}  // namespace cactus::runtime::entt_backend
