// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <variant>

namespace {

using Offspring        = commit_notify_loop_runtime__Offspring;
using Door             = commit_notify_loop_runtime__Door;
using Knock            = commit_notify_loop_runtime__KnockEvent;
using OffspringArrived = cactus::runtime::entt_backend::TraitAdded<Offspring>;

std::size_t offspring_count(entt::registry& registry) {
    return registry.view<Offspring>().size();
}

template <typename Event>
std::size_t delayed_count() {
    const auto& delayed = cactus::runtime::entt_backend::generated_scheduler_state().delayed_host;
    return static_cast<std::size_t>(std::ranges::count_if(
        delayed, [](const auto& queued) { return std::holds_alternative<Event>(queued.occurrence); }));
}

int knocks(entt::registry& registry, entt::entity door) {
    return registry.get<Door>(door).knocks;
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("self-feeding spawns and targeted chains advance one hop per host activation",
          "[runtime][commit-notify-loop]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots = generated_named_slots();
    const auto front  = slots.commit_notify_loop_runtime__Front;
    const auto back   = slots.commit_notify_loop_runtime__Back;

    // Load: Seed's spawn and the first knock reach their consumers at once;
    // the consumers' own follow-ups are on a cycle, so they wait.
    CHECK(offspring_count(registry) == 2);
    CHECK(delayed_count<OffspringArrived>() == 1);
    CHECK(knocks(registry, front) == 1);
    CHECK(delayed_count<Knock>() == 1);

    for (std::size_t frame = 1; frame <= 3; ++frame) {
        cactus_headless_test::drive_frame(registry);
        // The waiting arrival ran in this frame's drain and spawned one more.
        CHECK(offspring_count(registry) == 2 + frame);
        CHECK(delayed_count<OffspringArrived>() == 1);
        CHECK(knocks(registry, front) == static_cast<int>(1 + frame));
        CHECK(knocks(registry, back) == 0);
    }

    // A delayed knock whose recipient is gone is dropped, not broadcast.
    REQUIRE(delayed_count<Knock>() == 1);
    registry.destroy(front);
    cactus_headless_test::drive_frame(registry);
    CHECK(delayed_count<Knock>() == 0);
    CHECK(knocks(registry, back) == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
