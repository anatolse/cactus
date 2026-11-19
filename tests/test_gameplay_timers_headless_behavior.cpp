// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <limits>

namespace {

using cactus_headless_test::drive_frame;

// The one fixed-step length std.core.fixed_tick advances by. A frame driven
// with exactly this dt performs exactly one fixed step.
constexpr float kStep = 1.0F / 60.0F;
constexpr cactus::Duration kStepDuration{11'760'000};

constexpr cactus::Duration milliseconds(std::int64_t count) {
    return cactus::Duration{count * 705'600};
}

const cactus::Duration kBulletLifetime = milliseconds(40);

struct World {
    entt::registry registry;

    World() {
        // The scheduler is a process-wide singleton, so a leftover fixed-step
        // accumulator from an earlier TEST_CASE would change how many steps
        // this one's frames perform. Every assertion below counts steps.
        std::get<cactus_gen::stdlib::core::FixedTickPhase>(cactus::runtime::entt_backend::generated_scheduler_state().phases) = {};
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

// Queues this frame's timer requests on `timer`; ApplyTimerRequests turns them
// into std.time events during the input phase of the next driven frame.
void request_restart(entt::registry& registry, entt::entity timer, float duration) {
    auto& requests          = registry.get<cactus_gen::gameplay_timers::TimerRequests>(timer);
    requests.restarts       = 1;
    requests.first_duration = duration;
}

// FireBullets spawns during fixed_tick, so the bullet only becomes selectable
// at that step's activation commit — the returned bullet still has a full
// BULLET_LIFETIME remaining, because its first advance is the *next* eligible
// step, not the one it was created in.
entt::entity fire_one_bullet(entt::registry& registry) {
    registry.get<cactus_gen::gameplay_timers::Muzzle>(player_of(registry)).pending_shots = 1;
    drive_frame(registry, kStep);
    return entity_with<cactus_gen::gameplay_timers::Bullet>(registry);
}

}  // namespace

TEST_CASE("std.time lifetime destroys its entity on the fixed step that exhausts the countdown",
          "[runtime][stdlib][time][lifetime]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);
    CHECK(registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining == kBulletLifetime);

    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining == kBulletLifetime - kStepDuration);

    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining == kBulletLifetime - (kStepDuration * 2));
    CHECK(registry.valid(bullet));

    // 40ms - 2 steps is under one step, so this step exhausts and destroys it.
    drive_frame(registry, kStep);
    CHECK_FALSE(registry.valid(bullet));
    CHECK(registry.view<cactus_gen::gameplay_timers::Bullet>().begin() ==
          registry.view<cactus_gen::gameplay_timers::Bullet>().end());
}

TEST_CASE("std.time lifetime does not advance while paused, and resumes from where it stopped",
          "[runtime][stdlib][time][lifetime]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);

    registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).paused = true;

    // One frame worth three fixed steps: a paused lifetime consumes none of them.
    drive_frame(registry, 3.0F * kStep);
    CHECK(registry.valid(bullet));
    CHECK(registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining == kBulletLifetime);

    registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).paused = false;
    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining == kBulletLifetime - kStepDuration);
}

TEST_CASE("std.time lifetime consumes no time on a frame that performs no fixed step",
          "[runtime][stdlib][time][lifetime]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);

    // Well under one fixed interval: the phase batch runs zero repetitions.
    drive_frame(registry, kStep / 4.0F);
    CHECK(registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining == kBulletLifetime);
}

TEST_CASE("std.time lifetime does not simulate catch-up time the fixed phase dropped",
          "[runtime][stdlib][time][lifetime]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);

    registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining = milliseconds(1000);

    // A one-second frame is due 60 fixed steps but std.core.fixed_tick caps
    // repetitions at max: 8; the other 52 steps' worth of time is dropped and
    // must not be subtracted anyway.
    drive_frame(registry, 1.0F);
    CHECK(registry.valid(bullet));
    CHECK(registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining ==
          milliseconds(1000) - (kStepDuration * 8));
}

TEST_CASE("std.time lifetime ends at once when no time remains", "[runtime][stdlib][time][lifetime]") {
    World world;
    auto& registry = world.registry;

    const auto bullet = fire_one_bullet(registry);

    const auto remaining = GENERATE(milliseconds(0), milliseconds(-5000));
    registry.get<cactus_gen::stdlib::time::Lifetime>(bullet).remaining = remaining;

    drive_frame(registry, kStep);
    CHECK_FALSE(registry.valid(bullet));
}

TEST_CASE("std.time timers arm, expire once, and relay through their own entity",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto player       = player_of(registry);
    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    const auto shield_timer = entity_with<cactus_gen::gameplay_timers::ShieldChannel>(registry);

    // Each timer names its owner directly in its declaration.
    CHECK(registry.get<cactus_gen::gameplay_timers::TimerOwner>(reload_timer).owner == player);
    CHECK(registry.get<cactus_gen::gameplay_timers::TimerOwner>(shield_timer).owner == player);

    registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer).restarts = 1;
    registry.get<cactus_gen::gameplay_timers::TimerRequests>(shield_timer).restarts = 1;
    drive_frame(registry, kStep);

    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).armed);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(shield_timer).armed);

    // SHIELD_SECONDS (0.07) expires first; RELOAD_SECONDS (0.11) is still
    // running, so the two countdowns are genuinely independent.
    for (int step = 0; step < 4; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).shields_ready == 1);
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 0);
    CHECK_FALSE(registry.get<cactus_gen::stdlib::time::Timer>(shield_timer).armed);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).armed);

    for (int step = 0; step < 3; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 1);
    CHECK_FALSE(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).armed);

    // Neither timer fires a second time once disarmed.
    for (int step = 0; step < 20; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 1);
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).shields_ready == 1);
}

TEST_CASE("std.time restart aimed at an entity without a Timer is a no-match, not an implicit Timer",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto player = player_of(registry);
    request_restart(registry, player, 0.11F);

    for (int step = 0; step < 20; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.try_get<cactus_gen::stdlib::time::Timer>(player) == nullptr);
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 0);
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).shields_ready == 0);
}

TEST_CASE("std.time restart replaces the previous duration rather than adding to it",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer  = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    auto& requests           = registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer);
    requests.restarts        = 2;
    requests.first_duration  = 1.0F;
    requests.second_duration = 0.11F;

    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).remaining ==
          cactus::duration_from_seconds(0.11F) - kStepDuration);

    for (int step = 0; step < 6; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player_of(registry)).reloads_ready == 1);
}

TEST_CASE("std.time cancel disarms a restarted timer before it can expire", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer  = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    auto& requests           = registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer);
    requests.restarts        = 1;
    requests.cancel_pending  = true;

    drive_frame(registry, kStep);
    CHECK_FALSE(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).armed);

    for (int step = 0; step < 20; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player_of(registry)).reloads_ready == 0);
}

TEST_CASE("std.time expires a NaN or negative restart duration at the next fixed step",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    const float bad = GENERATE(std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::infinity(), -3.0F);
    request_restart(registry, reload_timer, bad);

    // duration(x) gives zero for NaN and a negative count otherwise: armed now,
    // expiring at the next eligible fixed step, not at the restart site.
    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).remaining == cactus::Duration{});
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player_of(registry)).reloads_ready == 1);
}

TEST_CASE("std.time keeps an infinite restart duration as the longest duration", "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    request_restart(registry, reload_timer, std::numeric_limits<float>::infinity());

    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).armed);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).remaining ==
          cactus::Duration{std::numeric_limits<std::int64_t>::max()} - kStepDuration);
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player_of(registry)).reloads_ready == 0);
}

TEST_CASE("std.time zero-duration timers expire at the next fixed step, not at the restart site",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto player       = player_of(registry);
    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    request_restart(registry, reload_timer, 0.0F);

    // No fixed step this frame, so the armed zero-duration timer has not run.
    drive_frame(registry, kStep / 4.0F);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).armed);
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 0);

    drive_frame(registry, kStep);
    CHECK_FALSE(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).armed);
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 1);
}

TEST_CASE("std.time timers restarted from their own expiry handler fire once per fixed step",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto metronome = entity_with<cactus_gen::gameplay_timers::Metronome>(registry);
    request_restart(registry, metronome, 0.0F);

    // RepeatMetronome rearms with duration 0s from inside TimerExpired. A
    // handler-driven restart must not re-expire within the same fixed step.
    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::gameplay_timers::Metronome>(metronome).expirations == 1);

    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::gameplay_timers::Metronome>(metronome).expirations == 2);

    // One frame worth three fixed steps: three more expirations, no more.
    drive_frame(registry, 3.0F * kStep);
    CHECK(registry.get<cactus_gen::gameplay_timers::Metronome>(metronome).expirations == 5);
}

TEST_CASE("std.time timers do not advance while paused and keep their replacement duration",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto player       = player_of(registry);
    const auto reload_timer = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).paused = true;
    request_restart(registry, reload_timer, 0.11F);

    for (int step = 0; step < 10; ++step) {
        drive_frame(registry, kStep);
    }
    // RestartTimer preserves paused, so the replacement duration is untouched.
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).paused);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).armed);
    CHECK(registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).remaining == cactus::duration_from_seconds(0.11F));
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 0);

    registry.get<cactus_gen::stdlib::time::Timer>(reload_timer).paused = false;
    for (int step = 0; step < 6; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 0);

    drive_frame(registry, kStep);
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player).reloads_ready == 1);
}

TEST_CASE("std.time cancels an armed countdown when its owning entity is destroyed",
          "[runtime][stdlib][time][timer]") {
    World world;
    auto& registry = world.registry;

    const auto reload_timer  = entity_with<cactus_gen::gameplay_timers::ReloadChannel>(registry);
    auto& requests           = registry.get<cactus_gen::gameplay_timers::TimerRequests>(reload_timer);
    requests.restarts        = 1;
    requests.destroy_pending = true;

    drive_frame(registry, kStep);
    REQUIRE_FALSE(registry.valid(reload_timer));

    // A fresh entity may reuse the destroyed one's storage slot; no expiration
    // may reach it, and the weapon must never see a reload it did not start.
    const auto recycled = registry.create();
    registry.emplace<cactus_gen::stdlib::time::Timer>(recycled);
    registry.emplace<cactus::runtime::entt_backend::CreationOrdinal>(
        recycled,
        cactus::runtime::entt_backend::CreationOrdinal{
            .value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});

    for (int step = 0; step < 20; ++step) {
        drive_frame(registry, kStep);
    }
    CHECK(registry.get<cactus_gen::gameplay_timers::Weapon>(player_of(registry)).reloads_ready == 0);
    CHECK_FALSE(registry.get<cactus_gen::stdlib::time::Timer>(recycled).armed);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
