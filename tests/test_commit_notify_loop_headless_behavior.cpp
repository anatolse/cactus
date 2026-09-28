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

using Offspring = commit_notify_loop_runtime__Offspring;
using Door      = commit_notify_loop_runtime__Door;
using Knock     = commit_notify_loop_runtime__KnockEvent;
using cactus::runtime::entt_backend::kMaxEventCascadeDepth;

// Rounds 1..bound deliver a notification that spawns again; round bound+1
// still applies its spawn but defers its notification.
constexpr std::size_t kSpawnsPerActivation = kMaxEventCascadeDepth + 1;

std::size_t offspring_count(entt::registry& registry) {
    return registry.view<Offspring>().size();
}

template <typename Event>
std::size_t deferred_count() {
    const auto& deferred = cactus::runtime::entt_backend::generated_scheduler_state().activation.deferred_events;
    return static_cast<std::size_t>(std::ranges::count_if(
        deferred, [](const auto& queued) { return std::holds_alternative<Event>(queued.occurrence); }));
}

int knocks(entt::registry& registry, entt::entity door) {
    return registry.get<Door>(door).knocks;
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("cascade overflow from commit rounds and targeted chains defers to the next frame",
          "[runtime][commit-notify-loop]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots = generated_named_slots();
    const auto front  = slots.commit_notify_loop_runtime__Front;
    const auto back   = slots.commit_notify_loop_runtime__Back;

    // Load: a bounded batch of spawns and a bounded knock chain, each
    // leaving exactly one deferred occurrence.
    CHECK(offspring_count(registry) == kSpawnsPerActivation);
    CHECK(deferred_count<std_core__spawnEvent>() == 1);
    CHECK(knocks(registry, front) == static_cast<int>(kMaxEventCascadeDepth));
    CHECK(deferred_count<Knock>() == 1);

    for (std::size_t frame = 1; frame <= 3; ++frame) {
        cactus_headless_test::drive_frame(registry);
        // The deferred notification ran this frame and its spawns committed here.
        CHECK(offspring_count(registry) == kSpawnsPerActivation * (frame + 1));
        CHECK(deferred_count<std_core__spawnEvent>() == 1);
        // The deferred knock reached only Front: the root knock plus a new chain.
        CHECK(knocks(registry, front) == static_cast<int>((frame * (kMaxEventCascadeDepth + 1)) + kMaxEventCascadeDepth));
        CHECK(knocks(registry, back) == 0);
    }

    // A deferred knock whose recipient is gone is dropped, not broadcast.
    REQUIRE(deferred_count<Knock>() == 1);
    registry.destroy(front);
    cactus_headless_test::drive_frame(registry);
    CHECK(deferred_count<Knock>() == 0);
    CHECK(knocks(registry, back) == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
