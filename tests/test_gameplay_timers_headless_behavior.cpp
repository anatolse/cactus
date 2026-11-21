// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <limits>

namespace {

using cactus_headless_test::drive_frame;
using Span = cactus_gen::stdlib::time::Span;

// The one fixed-step length std.core.fixed_tick advances by. A frame driven
// with exactly this dt performs exactly one fixed step.
constexpr float kStep = 1.0F / 60.0F;
constexpr cactus::Duration kStepDuration{11'760'000};

constexpr cactus::Duration milliseconds(std::int64_t count) {
    return cactus::Duration{count * 705'600};
}

struct World {
    entt::registry registry;

    World() {
        // The scheduler is a process-wide singleton, so a leftover fixed-step
        // accumulator from an earlier TEST_CASE would change how many steps
        // this one's frames perform. Every assertion below counts steps.
        cactus::runtime::entt_backend::generated_reset_scheduler_state();
        cactus_headless_test::init_and_load(registry);
    }
};

template <typename Trait>
entt::entity entity_with(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    REQUIRE(view.begin() != view.end());
    return *view.begin();
}

entt::entity player_of(entt::registry& registry) {
    return entity_with<cactus_gen::gameplay_timers::Weapon>(registry);
}

cactus_gen::gameplay_timers::Weapon& weapon_of(entt::registry& registry) {
    return registry.get<cactus_gen::gameplay_timers::Weapon>(player_of(registry));
}

// Queues a start on `timer`; ApplyTimerRequests turns it into a new span
// during the input phase of the next driven frame.
void request_start(entt::registry& registry, entt::entity timer, float duration) {
    auto& requests          = registry.get<cactus_gen::gameplay_timers::TimerRequests>(timer);
    requests.starts         = 1;
    requests.first_duration = duration;
}

// FireBullets spawns during fixed_tick, so the bullet only becomes selectable
// at that step's activation commit: the returned bullet has not advanced yet,
// because its first advance is the *next* eligible step.
entt::entity fire_one_bullet(entt::registry& registry) {
    registry.get<cactus_gen::gameplay_timers::Muzzle>(player_of(registry)).pending_shots = 1;
    drive_frame(registry, kStep);
    return entity_with<cactus_gen::gameplay_timers::Bullet>(registry);
}

}  // namespace

TEST_CASE("an expiring span destroys a bullet on the fixed step that reaches its end",
          "[runtime][stdlib][time][expires]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);
    CHECK(registry.get<Span>(bullet).elapsed == cactus::Duration{});

    drive_frame(registry, kStep);
    CHECK(registry.get<Span>(bullet).elapsed == kStepDuration);

    drive_frame(registry, kStep);
    CHECK(registry.get<Span>(bullet).elapsed == kStepDuration * 2);
    CHECK(registry.valid(bullet));

    // 40ms is under three steps, so this step reaches the end and destroys it.
    drive_frame(registry, kStep);
    CHECK_FALSE(registry.valid(bullet));
    CHECK(registry.view<cactus_gen::gameplay_timers::Bullet>().begin() ==
          registry.view<cactus_gen::gameplay_timers::Bullet>().end());
}

TEST_CASE("a stopped game clock keeps a bullet alive, and it resumes where it stopped",
          "[runtime][stdlib][time][expires]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);
    auto& pace =
        registry.get<cactus_gen::stdlib::core::Pace>(cactus_gen::detail::generated_named_slots().stdlib.core.Clock);

    pace.scale = 0.0F;
    drive_frame(registry, 3.0F * kStep);
    CHECK(registry.valid(bullet));
    CHECK(registry.get<Span>(bullet).elapsed == cactus::Duration{});

    pace.scale = 1.0F;
    drive_frame(registry, kStep);
    CHECK(registry.get<Span>(bullet).elapsed == kStepDuration);
}

TEST_CASE("a span consumes no time on a frame that performs no fixed step", "[runtime][stdlib][time][expires]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);

    // Well under one fixed interval: the phase batch runs zero repetitions.
    drive_frame(registry, kStep / 4.0F);
    CHECK(registry.get<Span>(bullet).elapsed == cactus::Duration{});
}

TEST_CASE("a span does not simulate catch-up time the fixed phase dropped", "[runtime][stdlib][time][expires]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);

    registry.get<Span>(bullet).length = milliseconds(1000);

    // A one-second frame is due 60 fixed steps but std.core.fixed_tick caps
    // repetitions at max: 8; the other 52 steps' worth of time is dropped and
    // must not be added anyway.
    drive_frame(registry, 1.0F);
    CHECK(registry.valid(bullet));
    CHECK(registry.get<Span>(bullet).elapsed == kStepDuration * 8);
}

TEST_CASE("an expiring span with no length ends at once", "[runtime][stdlib][time][expires]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);

    const auto length                 = GENERATE(milliseconds(0), milliseconds(-5000));
    registry.get<Span>(bullet).length = length;

    drive_frame(registry, kStep);
    CHECK_FALSE(registry.valid(bullet));
}

TEST_CASE("timer spans end once and relay through their own entity", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto player       = player_of(registry);
    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    const auto shield_timer = entity_with<cactus_gen::gameplay_timers::ShieldChannel>(registry);

    // Each timer names its owner directly in its declaration.
    CHECK(registry.get<cactus_gen::gameplay_timers::TimerOwner>(reload_timer).owner == player);
    CHECK(registry.get<cactus_gen::gameplay_timers::TimerOwner>(shield_timer).owner == player);
    CHECK_FALSE(registry.all_of<Span>(reload_timer));

    registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer).starts = 1;
    registry.get<cactus_gen::gameplay_timers::TimerRequests>(shield_timer).starts = 1;
    drive_frame(registry, kStep);

    CHECK(registry.get<Span>(reload_timer).elapsed == kStepDuration);
    CHECK(registry.get<Span>(shield_timer).elapsed == kStepDuration);

    // SHIELD_SECONDS (0.07) ends first; RELOAD_SECONDS (0.11) is still
    // running, so the two countdowns are genuinely independent.
    for (int step = 0; step < 4; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(weapon_of(registry).shields_ready == 1);
    CHECK(weapon_of(registry).reloads_ready == 0);
    CHECK(registry.get<Span>(shield_timer).elapsed == registry.get<Span>(shield_timer).length);

    for (int step = 0; step < 3; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(weapon_of(registry).reloads_ready == 1);

    // Neither span ends a second time.
    for (int step = 0; step < 20; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(weapon_of(registry).reloads_ready == 1);
    CHECK(weapon_of(registry).shields_ready == 1);
}

TEST_CASE("a rewind aimed at an entity without a span is a no-match, not an implicit span",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto player = player_of(registry);
    registry.get<cactus_gen::gameplay_timers::TimerRequests>(player).rewind_pending = true;

    for (int step = 0; step < 20; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.try_get<Span>(player) == nullptr);
    CHECK(weapon_of(registry).reloads_ready == 0);
    CHECK(weapon_of(registry).shields_ready == 0);
}

TEST_CASE("a second start replaces the first span rather than adding to it", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer  = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    auto& requests           = registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer);
    requests.starts          = 2;
    requests.first_duration  = 1.0F;
    requests.second_duration = 0.11F;

    drive_frame(registry, kStep);
    CHECK(registry.get<Span>(reload_timer).length == cactus::duration_from_seconds(0.11F));
    CHECK(registry.get<Span>(reload_timer).elapsed == kStepDuration);

    for (int step = 0; step < 6; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(weapon_of(registry).reloads_ready == 1);
}

TEST_CASE("a rewind runs a span again with the same length", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    request_start(registry, reload_timer, 0.11F);
    for (int step = 0; step < 4; ++step) {
        drive_frame(registry, kStep);
    }
    registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer).rewind_pending = true;
    drive_frame(registry, kStep);
    CHECK(registry.get<Span>(reload_timer).elapsed == kStepDuration);

    for (int step = 0; step < 5; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(weapon_of(registry).reloads_ready == 0);
    drive_frame(registry, kStep);
    CHECK(weapon_of(registry).reloads_ready == 1);
}

TEST_CASE("removing a span cancels it before it can end", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    auto& requests          = registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer);
    requests.starts         = 1;
    requests.cancel_pending = true;

    drive_frame(registry, kStep);
    CHECK_FALSE(registry.all_of<Span>(reload_timer));

    for (int step = 0; step < 20; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(weapon_of(registry).reloads_ready == 0);
}

TEST_CASE("a NaN or negative start ends at the next fixed step", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    const float bad = GENERATE(std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::infinity(), -3.0F);
    request_start(registry, reload_timer, bad);

    // duration(x) gives zero for NaN and a negative count otherwise, so the
    // span ends at the next eligible fixed step, not at the start site.
    drive_frame(registry, kStep);
    CHECK(weapon_of(registry).reloads_ready == 1);
    drive_frame(registry, kStep);
    CHECK(weapon_of(registry).reloads_ready == 1);
}

TEST_CASE("an infinite start keeps the longest duration", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    request_start(registry, reload_timer, std::numeric_limits<float>::infinity());

    drive_frame(registry, kStep);
    CHECK(registry.get<Span>(reload_timer).length == cactus::Duration{std::numeric_limits<std::int64_t>::max()});
    CHECK(registry.get<Span>(reload_timer).elapsed == kStepDuration);
    CHECK(weapon_of(registry).reloads_ready == 0);
}

TEST_CASE("an empty span ends at the next fixed step, not at the start site", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    request_start(registry, reload_timer, 0.0F);

    // No fixed step this frame, so the new span has not run.
    drive_frame(registry, kStep / 4.0F);
    CHECK(registry.all_of<Span>(reload_timer));
    CHECK(weapon_of(registry).reloads_ready == 0);

    drive_frame(registry, kStep);
    CHECK(weapon_of(registry).reloads_ready == 1);
}

TEST_CASE("an empty repeating span ends once per fixed step", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto metronome = entity_with<cactus_gen::gameplay_timers::Metronome>(registry);
    request_start(registry, metronome, 0.0F);

    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::gameplay_timers::Metronome>(metronome).wraps == 1);

    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::gameplay_timers::Metronome>(metronome).wraps == 2);

    // One frame worth three fixed steps: three more wraps, no more.
    drive_frame(registry, 3.0F * kStep);
    CHECK(registry.get<cactus_gen::gameplay_timers::Metronome>(metronome).wraps == 5);
}

TEST_CASE("destroying a timer entity cancels its span", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer  = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    auto& requests           = registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer);
    requests.starts          = 1;
    requests.destroy_pending = true;

    drive_frame(registry, kStep);
    REQUIRE_FALSE(registry.valid(reload_timer));

    // A fresh entity may reuse the destroyed one's storage slot; no ending may
    // reach the weapon through it.
    const auto recycled = registry.create();
    registry.emplace<Span>(recycled);
    registry.emplace<cactus::runtime::entt_backend::CreationOrdinal>(
        recycled,
        cactus::runtime::entt_backend::CreationOrdinal{
            .value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});

    for (int step = 0; step < 20; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(weapon_of(registry).reloads_ready == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
