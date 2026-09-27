// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using Match   = named_entity_access__Match;
using Stats   = named_entity_access__Stats;
using Control = named_entity_access__Control;
using Probe   = named_entity_access__Probe;
using Enemy   = named_entity_access__Enemy;
using Coin    = named_entity_access__Coin;
using Label   = named_entity_access__Label;
using Health  = named_entity_access__Health;
using Rival   = named_entity_access__Rival;
using Link    = named_entity_access__Link;
using Tag     = named_entity_access__Tag;

using cactus_headless_test::drive_frame;

template <typename Trait>
entt::entity only_entity(entt::registry& registry) {
    auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return *view.begin();
}

entt::entity tagged(entt::registry& registry, int id) {
    for (const auto entity : registry.view<Tag>()) {
        if (registry.get<Tag>(entity).id == id) {
            return entity;
        }
    }
    FAIL("no entity tagged " << id);
    return entt::null;
}

struct World {
    entt::registry registry;
    entt::entity game;
    entt::entity switch_entity;

    World() {
        cactus_headless_test::init_and_load(registry);
        game          = only_entity<Match>(registry);
        switch_entity = only_entity<Control>(registry);
    }

    Match& match() { return registry.get<Match>(game); }
    Control& control() { return registry.get<Control>(switch_entity); }
    Probe& probe() { return registry.get<Probe>(switch_entity); }

    [[nodiscard]] int total_steps() {
        int total = 0;
        for (const auto entity : registry.view<Enemy>()) {
            total += registry.get<Enemy>(entity).steps;
        }
        return total;
    }
};

}  // namespace

// ── Names as values ─────────────────────────────────────────────────────────

TEST_CASE("a name is a set target", "[runtime][named-entity]") {
    World world;
    const auto hud = only_entity<Label>(world.registry);
    REQUIRE(world.registry.get<Label>(hud).visible);

    world.control().hide_hud = true;
    drive_frame(world.registry);
    CHECK_FALSE(world.registry.get<Label>(hud).visible);
}

TEST_CASE("a name is an emit target and compares equal to its binding", "[runtime][named-entity]") {
    World world;
    const auto boss    = tagged(world.registry, 1);
    const auto nemesis = tagged(world.registry, 2);

    world.control().hit_boss = true;
    drive_frame(world.registry);

    const auto& boss_health = world.registry.get<Health>(boss);
    CHECK(boss_health.value == 9);
    CHECK(boss_health.was_boss);
    CHECK(boss_health.from_switch);
    CHECK(boss_health.marked);
    CHECK(world.registry.get<Health>(nemesis).value == 10);
}

TEST_CASE("a stale name as a value is a no-op target", "[runtime][named-entity]") {
    World world;
    const auto nemesis = tagged(world.registry, 2);

    world.control().destroy_boss = true;
    drive_frame(world.registry);
    REQUIRE(world.registry.view<Health>().size() == 1);

    world.control().hit_boss = true;
    drive_frame(world.registry);
    CHECK(world.control().hit_attempts == 1);
    CHECK(world.registry.get<Health>(nemesis).value == 10);
    CHECK_FALSE(world.registry.get<Health>(nemesis).marked);
}

TEST_CASE("names are archetype override values, including forward and mutual references",
          "[runtime][named-entity]") {
    World world;
    const auto boss    = tagged(world.registry, 1);
    const auto nemesis = tagged(world.registry, 2);
    const auto link_a  = tagged(world.registry, 3);
    const auto link_b  = tagged(world.registry, 4);

    CHECK(world.registry.get<Rival>(nemesis).rival == boss);
    CHECK(world.registry.get<Rival>(boss).rival == nemesis);
    CHECK(world.registry.get<Link>(link_a).other == link_b);
    CHECK(world.registry.get<Link>(link_b).other == link_a);
}

// ── Named field access ──────────────────────────────────────────────────────

TEST_CASE("a named read yields the field's current value", "[runtime][named-entity]") {
    World world;
    drive_frame(world.registry);
    CHECK_FALSE(world.probe().over_seen);

    world.match().over = true;
    drive_frame(world.registry);
    CHECK(world.probe().over_seen);
}

TEST_CASE("an immediate named write is visible to a later handler", "[runtime][named-entity]") {
    World world;
    world.control().add_score = true;
    drive_frame(world.registry);
    CHECK(world.match().score == 1);
    CHECK(world.probe().score_seen == 1);
}

TEST_CASE("accumulating into a named field over many entities is deterministic", "[runtime][named-entity]") {
    World world;
    int expected = 0;
    for (int value = 1; value <= 100; ++value) {
        world.registry.emplace<Coin>(world.registry.create(), Coin{.value = value});
        expected += value;
    }
    drive_frame(world.registry);
    CHECK(world.match().coins == expected);
    drive_frame(world.registry);
    CHECK(world.match().coins == 2 * expected);
}

// ── The implicit requirement ────────────────────────────────────────────────

TEST_CASE("handlers stop when the named entity is destroyed", "[runtime][named-entity]") {
    World world;
    drive_frame(world.registry);
    const auto steps          = world.total_steps();
    const auto match_passes   = world.probe().match_passes;
    const auto stats_passes   = world.probe().stats_passes;
    const auto branch_passes  = world.probe().branch_passes;
    const auto wave_ticks     = world.probe().wave_ticks;
    REQUIRE(steps == 3);

    world.control().destroy_game = true;
    drive_frame(world.registry);
    REQUIRE_FALSE(world.registry.valid(world.game));

    drive_frame(world.registry);
    drive_frame(world.registry);
    CHECK(world.total_steps() == steps + 3);
    CHECK(world.probe().match_passes == match_passes + 1);
    CHECK(world.probe().stats_passes == stats_passes + 1);
    CHECK(world.probe().wave_ticks == wave_ticks + 1);

    SECTION("the requirement covers the whole handler, even a name used in one branch") {
        CHECK(world.probe().branch_passes == branch_passes + 1);
    }
}

TEST_CASE("handlers stop when the named trait is removed, others keep running", "[runtime][named-entity]") {
    World world;
    world.control().strip_match = true;
    drive_frame(world.registry);
    REQUIRE(world.registry.valid(world.game));
    REQUIRE_FALSE(world.registry.all_of<Match>(world.game));

    const auto match_passes = world.probe().match_passes;
    const auto stats_passes = world.probe().stats_passes;
    const auto game_ticks   = world.registry.get<Stats>(world.game).ticks;
    drive_frame(world.registry);
    drive_frame(world.registry);
    CHECK(world.probe().match_passes == match_passes);
    CHECK(world.probe().stats_passes == stats_passes + 2);
    CHECK(world.registry.get<Stats>(world.game).ticks == game_ticks + 2);
}

TEST_CASE("destroy inside a pass does not break later reads of the name", "[runtime][named-entity]") {
    World world;
    world.match().score              = 42;
    world.control().destroy_then_read = true;
    drive_frame(world.registry);
    CHECK(world.control().read_after_destroy == 42);
    CHECK_FALSE(world.registry.valid(world.game));
}

// ── when: ───────────────────────────────────────────────────────────────────

TEST_CASE("when: false skips the whole filter pass", "[runtime][when]") {
    World world;
    drive_frame(world.registry);
    REQUIRE(world.total_steps() == 3);

    world.match().over = true;
    drive_frame(world.registry);
    CHECK(world.total_steps() == 3);
}

TEST_CASE("when: gates event handlers", "[runtime][when]") {
    World world;
    drive_frame(world.registry);
    const auto pings = world.probe().gated_pings;
    REQUIRE(pings >= 1);

    world.match().over = true;
    drive_frame(world.registry);
    CHECK(world.probe().gated_pings == pings);
}

TEST_CASE("multiple when: lines form a conjunction", "[runtime][when]") {
    World world;
    drive_frame(world.registry);
    REQUIRE(world.probe().wave_ticks == 1);

    world.match().wave = 0;
    drive_frame(world.registry);
    CHECK(world.probe().wave_ticks == 1);

    world.match().wave = 2;
    drive_frame(world.registry);
    CHECK(world.probe().wave_ticks == 2);

    world.match().over = true;
    drive_frame(world.registry);
    CHECK(world.probe().wave_ticks == 2);
}

TEST_CASE("when: observes an earlier writer in the same activation", "[runtime][when]") {
    World world;
    drive_frame(world.registry);
    const auto steps = world.total_steps();

    world.control().end_game = true;
    drive_frame(world.registry);
    CHECK(world.match().over);
    CHECK(world.total_steps() == steps);
}

TEST_CASE("a write during the pass does not close the gate for that pass", "[runtime][when]") {
    World world;
    world.control().close_mid_pass = true;
    drive_frame(world.registry);
    CHECK(world.match().over);
    for (const auto entity : world.registry.view<Enemy>()) {
        CHECK(world.registry.get<Enemy>(entity).closer_visits == 1);
    }

    drive_frame(world.registry);
    for (const auto entity : world.registry.view<Enemy>()) {
        CHECK(world.registry.get<Enemy>(entity).closer_visits == 1);
    }
}

TEST_CASE("a stale name in when: stops the rule", "[runtime][when]") {
    World world;
    drive_frame(world.registry);
    const auto pings = world.probe().gated_pings;

    world.control().destroy_game = true;
    drive_frame(world.registry);
    const auto after_commit = world.probe().gated_pings;
    drive_frame(world.registry);
    CHECK(after_commit == pings + 1);
    CHECK(world.probe().gated_pings == after_commit);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
