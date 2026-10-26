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

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
