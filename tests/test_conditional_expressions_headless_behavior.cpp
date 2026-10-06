// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

namespace {

using Actor = conditional_expressions__Actor;
using Mode  = conditional_expressions__Mode;

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

Actor run_one_frame(Mode mode, int hp) {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    const auto actor = registry.create();
    registry.emplace<Actor>(actor, Actor{.mode = mode, .hp = hp});
    drive_frame(registry);
    return registry.get<Actor>(actor);
}

}  // namespace

TEST_CASE("match expression and enum value match select the arm for each mode",
          "[runtime][codegen-entt][conditional-expressions]") {
    const auto idle = run_one_frame(Mode::Idle, 100);
    CHECK(idle.speed == 0.0F);
    CHECK(idle.label == 10);
    CHECK(idle.stamina == 0);

    const auto walk = run_one_frame(Mode::Walk, 100);
    CHECK(walk.speed == 1.5F);
    CHECK(walk.label == 20);
    CHECK(walk.stamina == 1);

    const auto run = run_one_frame(Mode::Run, 100);
    CHECK(run.speed == 4.0F);
    CHECK(run.label == 30);
    CHECK(run.stamina == 3);
}

TEST_CASE("if expression with else if and int value match select by hp",
          "[runtime][codegen-entt][conditional-expressions]") {
    const auto dead = run_one_frame(Mode::Idle, 0);
    CHECK(dead.tier == 0);
    CHECK(dead.bucket == -1);

    const auto hurt = run_one_frame(Mode::Idle, 30);
    CHECK(hurt.tier == 1);
    CHECK(hurt.bucket == 7);

    const auto full = run_one_frame(Mode::Idle, 100);
    CHECK(full.tier == 2);
    CHECK(full.bucket == 1);

    const auto healthy = run_one_frame(Mode::Idle, 60);
    CHECK(healthy.tier == 2);
    CHECK(healthy.bucket == 7);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
