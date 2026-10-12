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

constexpr float kStep = 1.0F / 60.0F;

int knocks(entt::registry& registry, entt::entity front) {
    return registry.get<delivery_clocks_runtime__Door>(front).knocks;
}

int seen(entt::registry& registry, entt::entity front) {
    return registry.get<delivery_clocks_runtime__Seen>(front).count;
}

int hops(entt::registry& registry, entt::entity front) {
    return registry.get<delivery_clocks_runtime__Echoes>(front).hops;
}

int chain_ends(entt::registry& registry, entt::entity stats) {
    return registry.get<delivery_clocks_runtime__Counter>(stats).chain_end;
}

std::size_t fixed_tick_delays() {
    return cactus::runtime::entt_backend::generated_scheduler_state().delayed_std_core__fixed_tick.size();
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("delayed occurrences run in the next activation of their clock", "[runtime][delivery-cycles]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots = generated_named_slots();
    const auto front  = slots.delivery_clocks_runtime__Front;
    const auto stats  = slots.delivery_clocks_runtime__Stats;

    // The load boundary's Echo ran once; its own Ping waits for the host drain.
    CHECK(hops(registry, front) == 1);
    CHECK(knocks(registry, front) == 0);

    // Three fixed_tick repetitions: one knock hop in each.
    cactus_headless_test::drive_frame(registry, 3.5F * kStep);
    CHECK(knocks(registry, front) == 3);
    // Observe is off the cycle, so it sees every Knock in the producing repetition.
    CHECK(seen(registry, front) == 4);
    CHECK(fixed_tick_delays() == 1);
    // The delay out of load reached this frame's drain, and only one hop of it.
    CHECK(hops(registry, front) == 2);
    // The acyclic 100-handler chain finished inside the frame's tick.
    CHECK(chain_ends(registry, stats) == 1);

    // No repetition this frame: the fixed_tick occurrence waits, the host one does not.
    cactus_headless_test::drive_frame(registry, 0.0F);
    CHECK(knocks(registry, front) == 3);
    CHECK(fixed_tick_delays() == 1);
    CHECK(hops(registry, front) == 3);
    CHECK(chain_ends(registry, stats) == 2);

    // The next repetition, in a later frame, picks the occurrence up.
    cactus_headless_test::drive_frame(registry, 1.0F * kStep);
    CHECK(knocks(registry, front) == 4);
    CHECK(seen(registry, front) == 5);
    CHECK(fixed_tick_delays() == 1);

    // Teardown: the unload boundary runs no delayed handler.
    cactus_headless_test::dispatch_unload_event(registry);
    CHECK(knocks(registry, front) == 4);
    CHECK(hops(registry, front) == 4);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
