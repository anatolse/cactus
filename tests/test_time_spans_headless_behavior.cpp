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

#include <cstdint>

namespace {

namespace game    = cactus_gen::time_spans;
namespace spans   = cactus_gen::stdlib::time;
namespace core    = cactus_gen::stdlib::core;
namespace backend = cactus::runtime::entt_backend;

using Catch::Approx;

constexpr float kStep = 1.0F / 60.0F;
constexpr cactus::Duration kStepDuration{11'760'000};

constexpr cactus::Duration steps(std::int64_t count) {
    return cactus::Duration{kStepDuration.units * count};
}

struct World {
    entt::registry registry;

    World() {
        backend::generated_reset_scheduler_state();
        backend::clear_runtime_faults();
        cactus_headless_test::init_and_load(registry);
    }

    static auto& slots() { return cactus_gen::detail::generated_named_slots(); }

    void frames(int count, float dt = kStep) {
        for (int frame = 0; frame < count; ++frame) {
            cactus_headless_test::drive_frame(registry, dt);
        }
    }

    [[nodiscard]] spans::Span& span(entt::entity entity) { return registry.get<spans::Span>(entity); }
    [[nodiscard]] game::Log& log(entt::entity entity) { return registry.get<game::Log>(entity); }
    [[nodiscard]] game::Requests& requests(entt::entity entity) { return registry.get<game::Requests>(entity); }
    [[nodiscard]] float& pace() { return registry.get<core::Pace>(slots().stdlib.core.Clock).scale; }
};

}  // namespace

TEST_CASE("a span's progress grows by one step at a time", "[runtime][stdlib-time-spans]") {
    World world;
    const auto plain = World::slots().time_spans.Plain;
    for (int step = 1; step <= 6; ++step) {
        world.frames(1);
        CHECK(world.span(plain).elapsed == steps(step));
        CHECK(world.log(plain).progress == Approx(static_cast<float>(step) / 6.0F));
    }
}

TEST_CASE("a plain span stops at its end and emits SpanEnded once", "[runtime][stdlib-time-spans]") {
    World world;
    const auto plain = World::slots().time_spans.Plain;
    world.frames(5);
    CHECK(world.log(plain).ended == 0);
    world.frames(1);
    CHECK(world.log(plain).ended == 1);
    world.frames(14);
    CHECK(world.log(plain).ended == 1);
    CHECK(world.span(plain).elapsed == world.span(plain).length);
    CHECK(world.log(plain).progress == 1.0F);
}

TEST_CASE("an expiring span destroys its entity on the exact step", "[runtime][stdlib-time-spans]") {
    World world;
    const auto gone = World::slots().time_spans.Gone;
    world.frames(5);
    REQUIRE(world.registry.valid(gone));
    CHECK(world.span(gone).elapsed == steps(5));
    CHECK(world.log(gone).ended == 0);
    world.frames(1);
    CHECK_FALSE(world.registry.valid(gone));
}

TEST_CASE("a repeating span wraps and emits on each wrap", "[runtime][stdlib-time-spans]") {
    World world;
    const auto loop = World::slots().time_spans.Loop;
    world.frames(12);
    CHECK(world.log(loop).ended == 2);
    CHECK(world.span(loop).elapsed == cactus::Duration{});
}

TEST_CASE("a repeating span keeps the overshoot past its end", "[runtime][stdlib-time-spans]") {
    World world;
    const auto loop = World::slots().time_spans.ShortLoop;
    const auto wait = world.span(loop).length;
    world.frames(2);
    CHECK(world.log(loop).ended == 1);
    CHECK(world.span(loop).elapsed == steps(2) - wait);
    world.frames(1);
    CHECK(world.log(loop).ended == 2);
    CHECK(world.span(loop).elapsed == cactus::Duration{});
}

TEST_CASE("a repeating span wraps at most once per step", "[runtime][stdlib-time-spans]") {
    World world;
    const auto loop = World::slots().time_spans.TinyLoop;
    world.frames(1);
    CHECK(world.log(loop).ended == 1);
    CHECK(world.span(loop).elapsed < world.span(loop).length);
    world.frames(3);
    CHECK(world.log(loop).ended == 4);
}

TEST_CASE("a bouncing span runs to its end and back", "[runtime][stdlib-time-spans]") {
    World world;
    const auto swing = World::slots().time_spans.Swing;
    world.frames(6);
    CHECK(world.log(swing).progress == 1.0F);
    world.frames(3);
    CHECK(world.span(swing).elapsed == steps(3));
    world.frames(3);
    CHECK(world.log(swing).progress == 0.0F);
    world.frames(2);
    CHECK(world.span(swing).elapsed == steps(2));
    CHECK(world.log(swing).ended == 0);
}

TEST_CASE("adding Ending.Repeats swaps an expiring span", "[runtime][stdlib-time-spans]") {
    World world;
    const auto gone             = World::slots().time_spans.Gone;
    world.requests(gone).repeat = true;
    world.frames(20);
    REQUIRE(world.registry.valid(gone));
    CHECK(world.registry.all_of<spans::Ending::Repeats>(gone));
    CHECK_FALSE(world.registry.all_of<spans::Ending::Expires>(gone));
    CHECK(world.log(gone).ended == 3);
}

TEST_CASE("RestartSpan runs an ended plain span again", "[runtime][stdlib-time-spans]") {
    World world;
    const auto plain = World::slots().time_spans.Plain;
    world.frames(7);
    REQUIRE(world.log(plain).ended == 1);
    world.requests(plain).restart = true;
    world.frames(1);
    CHECK(world.span(plain).elapsed == steps(1));
    CHECK(world.log(plain).ended == 1);
    world.frames(5);
    CHECK(world.log(plain).ended == 2);
}

TEST_CASE("RestartSpan runs a bouncing span forward from 0", "[runtime][stdlib-time-spans]") {
    World world;
    const auto swing = World::slots().time_spans.Swing;
    world.frames(8);
    REQUIRE(world.registry.get<spans::Ending::Bounces>(swing).back);
    world.requests(swing).restart = true;
    world.frames(1);
    CHECK_FALSE(world.registry.get<spans::Ending::Bounces>(swing).back);
    CHECK(world.span(swing).elapsed == steps(1));
}

TEST_CASE("a paused span does not advance, end or expire", "[runtime][stdlib-time-spans]") {
    World world;
    const auto held = World::slots().time_spans.Held;
    world.frames(20);
    REQUIRE(world.registry.valid(held));
    CHECK(world.span(held).elapsed == cactus::Duration{});
    CHECK(world.log(held).ended == 0);
    world.registry.remove<spans::Paused>(held);
    world.frames(6);
    CHECK_FALSE(world.registry.valid(held));
}

TEST_CASE("an empty span ends on its first step", "[runtime][stdlib-time-spans]") {
    World world;
    const auto& slots = World::slots().time_spans;
    world.frames(1);
    CHECK(world.log(slots.EmptyPlain).ended == 1);
    CHECK_FALSE(world.registry.valid(slots.EmptyGone));
    CHECK(world.log(slots.EmptyLoop).ended == 1);
    CHECK(world.registry.valid(slots.EmptySwing));

    world.frames(4);
    CHECK(world.log(slots.EmptyPlain).ended == 1);
    CHECK(world.log(slots.EmptyPlain).progress == 1.0F);
    CHECK(world.log(slots.EmptyLoop).ended == 5);
    CHECK(world.span(slots.EmptyLoop).elapsed == cactus::Duration{});
    CHECK(world.log(slots.EmptySwing).ended == 0);
    CHECK(world.log(slots.EmptySwing).progress == 1.0F);
}

TEST_CASE("a stopped game clock stops game-time spans, not real-time ones", "[runtime][stdlib-time-spans]") {
    World world;
    const auto& slots = World::slots().time_spans;
    world.pace()      = 0.0F;
    world.frames(60);
    CHECK(world.span(slots.GameClock).elapsed == cactus::Duration{});
    CHECK(world.span(slots.RealClock).elapsed ==
          cactus::duration_from_seconds(cactus::runtime::clean_frame_dt(kStep)) * 60);
}

TEST_CASE("a doubled game clock runs game-time spans twice as fast", "[runtime][stdlib-time-spans]") {
    World world;
    const auto& slots = World::slots().time_spans;
    world.pace()      = 2.0F;
    world.frames(30);
    CHECK(world.span(slots.GameClock).elapsed == steps(60));
}

TEST_CASE("a half-speed game clock runs game-time spans half as fast", "[runtime][stdlib-time-spans]") {
    World world;
    const auto& slots = World::slots().time_spans;
    world.pace()      = 0.5F;
    world.frames(60);
    CHECK(world.span(slots.GameClock).elapsed == steps(30));
    CHECK(world.span(slots.RealClock).elapsed ==
          cactus::duration_from_seconds(cactus::runtime::clean_frame_dt(kStep)) * 60);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
