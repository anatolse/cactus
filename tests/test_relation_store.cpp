// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"
#include "heap_allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using cactus::runtime::entt_backend::connect_relation_ends;
using cactus::runtime::entt_backend::relation_store;
using cactus::runtime::entt_backend::RelationStore;

namespace {

struct Tile {};
struct Robot {};
struct Task {};

struct WalkRow {
    static constexpr bool kFromOne = false;
    static constexpr bool kToOne   = false;
    using FromTrait                = Tile;
    using ToTrait                  = Tile;
    float cost                     = 1.0F;
};

struct ClaimRow {
    static constexpr bool kFromOne = true;
    static constexpr bool kToOne   = true;
    using FromTrait                = Robot;
    using ToTrait                  = Task;
};

std::vector<entt::entity> make_entities(entt::registry& registry, std::size_t count) {
    std::vector<entt::entity> entities(count);
    registry.create(entities.begin(), entities.end());
    return entities;
}

template <typename Store>
std::vector<std::pair<entt::entity, entt::entity>> pairs_of(const Store& store) {
    std::vector<std::pair<entt::entity, entt::entity>> pairs;
    for (const auto& entry : store.rows()) {
        pairs.emplace_back(entry.from, entry.to);
    }
    return pairs;
}

}  // namespace

TEST_CASE("RelationStore: add inserts rows in creation order", "[runtime][relations]") {
    entt::registry registry;
    const auto e = make_entities(registry, 3);
    RelationStore<WalkRow> store;
    store.add(e[0], e[1], WalkRow{.cost = 2.0F});
    store.add(e[1], e[0], WalkRow{});
    store.add(e[2], e[2], WalkRow{});
    CHECK(store.size() == 3);
    CHECK(store.contains(e[0], e[1]));
    CHECK(store.contains(e[1], e[0]));
    CHECK(store.contains(e[2], e[2]));
    CHECK_FALSE(store.contains(e[0], e[2]));
    REQUIRE(store.find(e[0], e[1]) != nullptr);
    CHECK(store.find(e[0], e[1])->cost == 2.0F);
    CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{
                                 {e[0], e[1]}, {e[1], e[0]}, {e[2], e[2]}});
}

TEST_CASE("RelationStore: replace keeps the creation position", "[runtime][relations]") {
    entt::registry registry;
    const auto e = make_entities(registry, 3);
    RelationStore<WalkRow> store;
    store.add(e[0], e[1], WalkRow{.cost = 1.0F});
    store.add(e[1], e[2], WalkRow{.cost = 1.0F});
    store.add(e[0], e[1], WalkRow{.cost = 3.0F});
    CHECK(store.size() == 2);
    CHECK(store.rows()[0].from == e[0]);
    CHECK(store.rows()[0].row.cost == 3.0F);
}

TEST_CASE("RelationStore: erase by pair and by end", "[runtime][relations]") {
    entt::registry registry;
    const auto e = make_entities(registry, 4);
    RelationStore<WalkRow> store;
    store.add(e[0], e[1], WalkRow{});
    store.add(e[1], e[2], WalkRow{});
    store.add(e[0], e[2], WalkRow{});
    store.add(e[3], e[0], WalkRow{});

    SECTION("pair") {
        store.erase(e[1], e[2]);
        store.erase(e[2], e[3]);  // no such row
        store.compact();
        CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{
                                     {e[0], e[1]}, {e[0], e[2]}, {e[3], e[0]}});
    }
    SECTION("from end") {
        store.erase_from(e[0]);
        CHECK_FALSE(store.has_from(e[0]));
        store.compact();
        CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{{e[1], e[2]}, {e[3], e[0]}});
    }
    SECTION("to end") {
        store.erase_to(e[2]);
        CHECK_FALSE(store.has_to(e[2]));
        store.compact();
        CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{{e[0], e[1]}, {e[3], e[0]}});
    }
    SECTION("indexes stay right after compaction") {
        store.erase(e[0], e[1]);
        store.compact();
        CHECK(store.contains(e[1], e[2]));
        CHECK(store.contains(e[3], e[0]));
        CHECK(store.has_from(e[0]));
        CHECK(store.has_to(e[0]));
        std::vector<entt::entity> targets;
        store.for_each_from(e[0], [&](const auto& entry) { targets.push_back(entry.to); });
        CHECK(targets == std::vector<entt::entity>{e[2]});
        store.add(e[0], e[1], WalkRow{});
        CHECK(pairs_of(store).back() == std::pair{e[0], e[1]});
    }
}

TEST_CASE("RelationStore: wildcard lookups", "[runtime][relations]") {
    entt::registry registry;
    const auto e = make_entities(registry, 3);
    RelationStore<WalkRow> store;
    store.add(e[0], e[1], WalkRow{});
    store.add(e[0], e[2], WalkRow{});
    CHECK(store.has_from(e[0]));
    CHECK_FALSE(store.has_from(e[1]));
    CHECK(store.has_to(e[1]));
    CHECK(store.has_to(e[2]));
    CHECK_FALSE(store.has_to(e[0]));
    std::vector<entt::entity> targets;
    store.for_each_from(e[0], [&](const auto& entry) { targets.push_back(entry.to); });
    CHECK(targets == std::vector<entt::entity>{e[1], e[2]});
    std::vector<entt::entity> sources;
    store.for_each_to(e[2], [&](const auto& entry) { sources.push_back(entry.from); });
    CHECK(sources == std::vector<entt::entity>{e[0]});
}

TEST_CASE("RelationStore: one ends hold a single slot", "[runtime][relations]") {
    entt::registry registry;
    const auto e = make_entities(registry, 4);
    RelationStore<ClaimRow> store;
    store.add(e[0], e[1], ClaimRow{});
    store.add(e[2], e[3], ClaimRow{});
    CHECK(store.has_from(e[0]));
    CHECK(store.has_to(e[3]));
    store.erase_from(e[0]);
    CHECK_FALSE(store.has_to(e[1]));
    store.add(e[0], e[1], ClaimRow{});
    CHECK(store.contains(e[0], e[1]));
    CHECK(store.one_ends_hold());
    store.compact();
    CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{{e[2], e[3]}, {e[0], e[1]}});
}

TEST_CASE("RelationStore: a keep run stamps rows and drops the rest", "[runtime][relations]") {
    entt::registry registry;
    const auto e = make_entities(registry, 3);
    RelationStore<WalkRow> store;
    store.begin_run();
    store.keep(e[0], e[1], WalkRow{.cost = 1.0F});
    store.keep(e[1], e[2], WalkRow{.cost = 1.0F});
    store.drop_unstamped();
    store.compact();
    CHECK(store.size() == 2);

    store.begin_run();
    store.keep(e[2], e[0], WalkRow{});
    store.keep(e[1], e[2], WalkRow{.cost = 5.0F});
    store.drop_unstamped();
    store.compact();
    CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{{e[1], e[2]}, {e[2], e[0]}});
    CHECK(store.find(e[1], e[2])->cost == 5.0F);
}

TEST_CASE("RelationStore: a steady add/remove cycle does not allocate", "[runtime][relations]") {
    entt::registry registry;
    const auto e = make_entities(registry, 64);
    RelationStore<WalkRow> store;
    const auto cycle = [&] {
        for (std::size_t index = 0; index + 1 < e.size(); ++index) {
            store.add(e[index], e[index + 1], WalkRow{});
        }
        for (std::size_t index = 0; index + 1 < e.size(); index += 2) {
            store.erase(e[index], e[index + 1]);
        }
        store.erase_from(e[1]);
        store.compact();
        for (std::size_t index = 0; index + 1 < e.size(); ++index) {
            store.erase(e[index], e[index + 1]);
        }
        store.begin_run();
        store.keep(e[3], e[4], WalkRow{});
        store.drop_unstamped();
        store.compact();
        store.erase_from(e[3]);
        store.compact();
    };
    cycle();
    cycle();
    const cactus_test::AllocationCount allocations;
    cycle();
    CHECK(cactus_test::AllocationCount::count() == 0);
}

TEST_CASE("RelationStore: rows die with an end trait or an end entity", "[runtime][relations]") {
    entt::registry registry;
    connect_relation_ends<ClaimRow>(registry);
    connect_relation_ends<WalkRow>(registry);
    const auto e = make_entities(registry, 4);
    registry.emplace<Robot>(e[0]);
    registry.emplace<Task>(e[1]);
    registry.emplace<Robot>(e[2]);
    registry.emplace<Task>(e[3]);
    for (const auto entity : e) {
        registry.emplace<Tile>(entity);
    }
    auto& claims = relation_store<ClaimRow>(registry);
    auto& walks  = relation_store<WalkRow>(registry);
    claims.add(e[0], e[1], ClaimRow{});
    claims.add(e[2], e[3], ClaimRow{});
    walks.add(e[0], e[1], WalkRow{});
    walks.add(e[1], e[2], WalkRow{});
    walks.add(e[2], e[3], WalkRow{});

    SECTION("losing the end trait") {
        registry.remove<Task>(e[1]);
        CHECK_FALSE(claims.contains(e[0], e[1]));
        CHECK(claims.contains(e[2], e[3]));
        CHECK_FALSE(claims.has_from(e[0]));
    }
    SECTION("destroying an end") {
        registry.destroy(e[2]);
        CHECK_FALSE(claims.has_from(e[2]));
        CHECK(claims.contains(e[0], e[1]));
        CHECK(walks.contains(e[0], e[1]));
        CHECK_FALSE(walks.contains(e[1], e[2]));
        CHECK_FALSE(walks.contains(e[2], e[3]));
    }
    SECTION("one trait on both ends") {
        registry.remove<Tile>(e[1]);
        CHECK_FALSE(walks.contains(e[0], e[1]));
        CHECK_FALSE(walks.contains(e[1], e[2]));
        CHECK(walks.contains(e[2], e[3]));
    }
}
TEST_CASE("RelationStore: commit commands apply in order and check their ends", "[runtime][relations]") {
    entt::registry registry;
    connect_relation_ends<ClaimRow>(registry);
    const auto e = make_entities(registry, 3);
    registry.emplace<Robot>(e[0]);
    registry.emplace<Task>(e[1]);
    registry.emplace<Task>(e[2]);

    SECTION("add then remove leaves no row") {
        cactus::runtime::entt_backend::add_relation_row(registry, e[0], e[1], ClaimRow{});
        relation_store<ClaimRow>(registry).erase(e[0], e[1]);
        CHECK_FALSE(relation_store<ClaimRow>(registry).contains(e[0], e[1]));
    }
    SECTION("an add after its end died does nothing") {
        registry.destroy(e[1]);
        cactus::runtime::entt_backend::add_relation_row(registry, e[0], e[1], ClaimRow{});
        CHECK(relation_store<ClaimRow>(registry).size() == 0);
    }
    SECTION("an add to an end without its end trait does nothing") {
        cactus::runtime::entt_backend::add_relation_row(registry, e[1], e[2], ClaimRow{});
        CHECK(relation_store<ClaimRow>(registry).size() == 0);
    }
}

TEST_CASE("RelationStore: kept commit commands validate both ends", "[runtime][relations][row-projection]") {
    entt::registry registry;
    connect_relation_ends<ClaimRow>(registry);
    const auto e = make_entities(registry, 2);
    registry.emplace<Robot>(e[0]);
    registry.emplace<Task>(e[1]);
    auto& store = relation_store<ClaimRow>(registry);
    store.begin_run();
    SECTION("valid ends") {
        cactus::runtime::entt_backend::keep_relation_row(registry, e[0], e[1], ClaimRow{});
        cactus::runtime::entt_backend::keep_relation_row(registry, e[0], e[1], ClaimRow{});
        store.drop_unstamped();
        CHECK(store.size() == 1);
        CHECK(store.contains(e[0], e[1]));
        return;
    }
    SECTION("destroyed from end") { registry.destroy(e[0]); }
    SECTION("destroyed to end") { registry.destroy(e[1]); }
    SECTION("missing from trait") { registry.remove<Robot>(e[0]); }
    SECTION("missing to trait") { registry.remove<Task>(e[1]); }
    cactus::runtime::entt_backend::keep_relation_row(registry, e[0], e[1], ClaimRow{});
    store.drop_unstamped();
    CHECK(store.size() == 0);
}

TEST_CASE("RelationStore: exact entries and collected joins preserve row order", "[runtime][relations][row-join]") {
    entt::registry registry;
    const auto e = make_entities(registry, 4);
    RelationStore<WalkRow> left;
    RelationStore<WalkRow> right;
    left.add(e[2], e[1], WalkRow{.cost = 2.0F});
    left.add(e[0], e[1], WalkRow{.cost = 1.0F});
    right.add(e[1], e[3], WalkRow{.cost = 3.0F});
    right.add(e[1], e[2], WalkRow{.cost = 4.0F});
    const auto* entry = left.find_entry(e[2], e[1]);
    REQUIRE(entry != nullptr);
    CHECK(entry->row.cost == 2.0F);
    CHECK(left.order_of(*entry) == 0);
    CHECK(left.find_entry(e[1], e[2]) == nullptr);
    const auto enumerate = [&](const auto& accept) {
        for (const auto& b : right.rows()) {
            left.for_each_to(b.from, [&](const auto& a) { accept(a, b); });
        }
    };
    const auto matches = cactus::runtime::collect_relation_join<true>(
        left, right, enumerate, [](const auto& a, const auto& b) { return a.row.cost + b.row.cost < 6.0F; });
    REQUIRE(matches.size() == 3);
    CHECK(matches[0].first->from == e[2]);
    CHECK(matches[0].second->to == e[3]);
    CHECK(matches[1].first->from == e[0]);
    CHECK(matches[1].second->to == e[3]);
    CHECK(matches[2].second->to == e[2]);
    const auto empty = cactus::runtime::collect_relation_join<false>(
        left, right, enumerate, [](const auto&, const auto&) { return false; });
    CHECK(empty.empty());
}

namespace {

struct CountedRows {
    using Entry = RelationStore<WalkRow>::Entry;
    RelationStore<WalkRow> store;
    std::size_t probes  = 0;
    std::size_t visited = 0;

    template <typename Visit>
    void for_each_from(entt::entity key, const Visit& visit) {
        ++probes;
        store.for_each_from(key, [&](const Entry& entry) {
            ++visited;
            visit(entry);
        });
    }

    template <typename Visit>
    void for_each_to(entt::entity key, const Visit& visit) {
        ++probes;
        store.for_each_to(key, [&](const Entry& entry) {
            ++visited;
            visit(entry);
        });
    }

    const Entry* find_entry(entt::entity from, entt::entity to) {
        ++probes;
        return store.find_entry(from, to);
    }

    auto rows() {
        visited += store.size();
        return store.rows();
    }
};

}  // namespace

TEST_CASE("Relation match lookups visit only their endpoint buckets", "[runtime][relations][row-join]") {
    entt::registry registry;
    const auto entities = make_entities(registry, 4098);
    CountedRows rows;
    for (std::size_t index = 0; index < 2048; ++index) {
        rows.store.add(entities[index], entities[index + 2048], WalkRow{});
    }
    std::size_t matches = 0;
    const auto accept   = [&](const auto&) { ++matches; };
    for (std::size_t index = 0; index < 2048; ++index) {
        cactus::runtime::for_each_relation_match<true, false>(rows, entities[index], 0, accept);
    }
    CHECK(matches == 2048);
    CHECK(rows.probes == 2048);
    CHECK(rows.visited == 2048);
    cactus::runtime::for_each_relation_match<false, true>(rows, 0, entities[2048], accept);
    CHECK(matches == 2049);
    cactus::runtime::for_each_relation_match<true, true>(rows, entities[0], entities[2048], accept);
    CHECK(matches == 2050);
    cactus::runtime::for_each_relation_match<true, true>(rows, entities[0], entities[2049], accept);
    CHECK(matches == 2050);
    cactus::runtime::for_each_relation_match<false, false>(rows, 0, 0, accept);
    CHECK(matches == 4098);
}

TEST_CASE("Relation binding predicates are evaluated once per snapshot row", "[runtime][relations][row-join]") {
    using Entry = RelationStore<WalkRow>::Entry;
    Entry accepted{.row = WalkRow{.cost = 2.0F}};
    Entry rejected{.row = WalkRow{.cost = 0.0F}};
    int calls      = 0;
    auto predicate = cactus::runtime::memoize_relation_value<Entry>([&](const Entry& entry) {
        ++calls;
        return entry.row.cost > 0.0F;
    });
    CHECK(predicate(accepted));
    CHECK(predicate(accepted));
    CHECK_FALSE(predicate(rejected));
    CHECK_FALSE(predicate(rejected));
    CHECK(calls == 2);
}

TEST_CASE("Relation binding sort keys are evaluated once per snapshot row", "[runtime][relations][row-join]") {
    using Entry = RelationStore<WalkRow>::Entry;
    Entry first{.row = WalkRow{.cost = 2.0F}};
    Entry second{.row = WalkRow{.cost = 3.0F}};
    int calls = 0;
    auto key = cactus::runtime::memoize_relation_value<Entry>([&](const Entry& entry) {
        ++calls;
        return entry.row.cost;
    });
    CHECK(key(first) == 2.0F);
    CHECK(key(second) == 3.0F);
    CHECK(key(first) == 2.0F);
    CHECK(key(second) == 3.0F);
    CHECK(calls == 2);
}


TEST_CASE("RelationStore: a batched keep run keeps rows in buffer order", "[runtime][relations][keep]") {
    using cactus::runtime::entt_backend::apply_relation_keep_run;
    using cactus::runtime::entt_backend::KeptRows;
    entt::registry registry;
    connect_relation_ends<WalkRow>(registry);
    const auto e = make_entities(registry, 4);
    for (const auto entity : e) {
        registry.emplace<Tile>(entity);
    }
    auto& store = relation_store<WalkRow>(registry);
    store.add(e[3], e[0], WalkRow{});

    const KeptRows<WalkRow> first{{.from = e[0], .to = e[2], .row = WalkRow{}},
                                  {.from = e[1], .to = e[0], .row = WalkRow{.cost = 2.0F}},
                                  {.from = e[1], .to = e[2], .row = WalkRow{}}};
    apply_relation_keep_run(registry, first);
    store.compact();
    CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{{e[0], e[2]}, {e[1], e[0]}, {e[1], e[2]}});
    CHECK(store.find(e[1], e[0])->cost == 2.0F);

    SECTION("a run that keeps every row drops none") {
        apply_relation_keep_run(registry, first);
        store.compact();
        CHECK(store.size() == 3);
    }
    SECTION("a run that keeps a subset drops the rest") {
        apply_relation_keep_run(registry, KeptRows<WalkRow>{{.from = e[1], .to = e[2], .row = WalkRow{}}});
        store.compact();
        CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{{e[1], e[2]}});
    }
    SECTION("a row erased after its stamp does not hide an unstamped row") {
        store.begin_run();
        store.keep(e[0], e[2], WalkRow{});
        store.keep(e[1], e[0], WalkRow{.cost = 2.0F});
        store.erase(e[0], e[2]);
        store.drop_unstamped();
        store.compact();
        CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{{e[1], e[0]}});
    }
    SECTION("a row whose end lost its trait is not kept") {
        registry.remove<Tile>(e[2]);
        apply_relation_keep_run(registry, first);
        store.compact();
        CHECK(pairs_of(store) == std::vector<std::pair<entt::entity, entt::entity>>{{e[1], e[0]}});
    }
}

namespace {
struct Lit {};
struct Glow {
    int level = 0;
};
}  // namespace

TEST_CASE("Kept trait batches add, patch and remove in buffer order", "[runtime][keep]") {
    using cactus::runtime::entt_backend::apply_kept_batch;
    using cactus::runtime::entt_backend::apply_unkept_batch;
    entt::registry registry;
    const auto e = make_entities(registry, 3);
    std::vector<entt::entity> added;
    registry.on_construct<Lit>().connect<[](std::vector<entt::entity>& into, entt::registry&, entt::entity entity) {
        into.push_back(entity);
    }>(added);
    apply_kept_batch<Lit, false>(registry, std::vector<entt::entity>{e[2], e[0], e[2]});
    CHECK(added == std::vector<entt::entity>{e[2], e[0]});
    apply_unkept_batch<Lit, false>(registry, std::vector<entt::entity>{e[0], e[1]});
    CHECK(registry.all_of<Lit>(e[2]));
    CHECK_FALSE(registry.all_of<Lit>(e[0]));

    registry.emplace<Glow>(e[1], Glow{.level = 1});
    apply_kept_batch<Glow, false>(registry, std::vector<std::pair<entt::entity, Glow>>{{e[1], Glow{.level = 4}}, {e[0], Glow{.level = 2}}});
    CHECK(registry.get<Glow>(e[1]).level == 4);
    CHECK(registry.get<Glow>(e[0]).level == 2);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
