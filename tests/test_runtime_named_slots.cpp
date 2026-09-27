// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cactus::runtime::entt_backend;

namespace {

struct Match {
    int score = 0;
};

struct Stats {
    int ticks = 0;
};

}  // namespace

TEST_CASE("named_alive requires a live entity carrying every trait", "[runtime][named-entity]") {
    entt::registry registry;
    const auto game = registry.create();
    registry.emplace<Match>(game);

    CHECK(named_alive<Match>(registry, game));
    CHECK_FALSE(named_alive<Match, Stats>(registry, game));
    CHECK_FALSE(named_alive<Match>(registry, entt::null));

    registry.emplace<Stats>(game);
    CHECK(named_alive<Match, Stats>(registry, game));

    registry.destroy(game);
    CHECK_FALSE(named_alive<Match>(registry, game));
}

TEST_CASE("adopt_or_create reuses a live handle and creates a free one", "[runtime][named-entity]") {
    entt::registry registry;
    const auto live = registry.create();
    CHECK(adopt_or_create(registry, live) == live);

    const auto free = entt::entity{42};
    REQUIRE_FALSE(registry.valid(free));
    CHECK(adopt_or_create(registry, free) == free);
    CHECK(registry.valid(free));
}

TEST_CASE("rebind_named_slots points each slot at its declaration's restored instance", "[runtime][named-entity]") {
    entt::registry registry;
    const auto first  = registry.create();
    const auto second = registry.create();
    registry.emplace<ArchetypeOrigin>(first, ArchetypeOrigin{.node = 3});
    registry.emplace<ArchetypeOrigin>(second, ArchetypeOrigin{.node = 7});

    entt::entity at_seven = first;
    entt::entity at_three = entt::null;
    entt::entity missing  = first;
    rebind_named_slots(registry,
                       {{.node = 7, .slot = &at_seven}, {.node = 3, .slot = &at_three}, {.node = 5, .slot = &missing}});

    CHECK(at_seven == second);
    CHECK(at_three == first);
    CHECK(missing == entt::entity{entt::null});
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
