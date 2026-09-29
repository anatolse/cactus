// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using Target = deterministic_target_selection__Target;
using Enemy  = deterministic_target_selection__Enemy;

const Target& target_of(entt::registry& registry, entt::entity tower) {
    return registry.get<Target>(tower);
}

int hits_of(entt::registry& registry, entt::entity enemy) {
    return registry.get<Enemy>(enemy).hits;
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("reset, select and consume keep each tower's target current", "[runtime][codegen-entt][rule-limit]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots = generated_named_slots();

    const auto near_tower  = slots.deterministic_target_selection__NearTower;
    const auto shadow      = slots.deterministic_target_selection__Shadow;
    const auto near        = slots.deterministic_target_selection__Near;
    const auto far         = slots.deterministic_target_selection__Far;
    const auto tie_tower   = slots.deterministic_target_selection__TieTower;
    const auto tie_first   = slots.deterministic_target_selection__TieFirst;
    const auto tie_second  = slots.deterministic_target_selection__TieSecond;
    const auto lone_tower  = slots.deterministic_target_selection__LoneTower;
    const auto lone        = slots.deterministic_target_selection__Lone;
    const auto empty_tower = slots.deterministic_target_selection__EmptyTower;

    cactus_headless_test::drive_frame(registry);

    // Nearest visible enemy wins over a nearer cloaked one and a farther one.
    CHECK(target_of(registry, near_tower).has_target);
    CHECK(target_of(registry, near_tower).selected == near);
    CHECK(hits_of(registry, near) == 1);
    CHECK(hits_of(registry, shadow) == 0);
    CHECK(hits_of(registry, far) == 0);

    // Equal distance: the earlier-created enemy wins.
    CHECK(target_of(registry, tie_tower).selected == tie_first);
    CHECK(hits_of(registry, tie_first) == 1);
    CHECK(hits_of(registry, tie_second) == 0);

    // Towers select independently in the same pass.
    CHECK(target_of(registry, lone_tower).selected == lone);
    CHECK(hits_of(registry, lone) == 1);

    // No candidate in range: no activation, the reset flag stays false.
    CHECK_FALSE(target_of(registry, empty_tower).has_target);

    // The last candidate disappears: the next reset clears the flag and
    // nothing selects again, so the old handle is never consumed.
    registry.destroy(lone);
    cactus_headless_test::drive_frame(registry);
    CHECK_FALSE(target_of(registry, lone_tower).has_target);
    CHECK(target_of(registry, near_tower).has_target);
    CHECK(hits_of(registry, near) == 2);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
