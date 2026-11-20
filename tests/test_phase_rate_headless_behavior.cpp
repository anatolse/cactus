// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>

namespace {

namespace game    = cactus_gen::phase_rate;
namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        backend::generated_reset_scheduler_state();
        backend::clear_runtime_faults();
        cactus_headless_test::init_and_load(registry);
    }

    static auto& slots() { return cactus_gen::detail::generated_named_slots().phase_rate; }

    void frames(int count) {
        for (int frame = 0; frame < count; ++frame) {
            cactus_headless_test::drive_frame(registry);
        }
    }

    [[nodiscard]] game::Log& log() { return registry.get<game::Log>(slots().Meter); }
    [[nodiscard]] float& rate() { return registry.get<game::Speed>(slots().Gear).rate; }

    // Steps `sim` ran during `count` frames.
    int steps_in(int count) {
        const int before = log().steps;
        frames(count);
        return log().steps - before;
    }
};

std::size_t rate_faults() {
    return static_cast<std::size_t>(std::ranges::count(
        backend::runtime_faults(), backend::RangeFaultReason::MissingRateSource, &backend::RangeFault::reason));
}

}  // namespace

TEST_CASE("rate 1.0 steps once per frame, as without rate:", "[runtime][phase-rate]") {
    World world;
    CHECK(world.steps_in(60) == 60);
}

TEST_CASE("rate 0.5 steps every other frame", "[runtime][phase-rate]") {
    World world;
    world.rate() = 0.5F;
    CHECK(world.steps_in(60) == 30);
}

TEST_CASE("rate 0 stops the phase and freezes alpha", "[runtime][phase-rate]") {
    World world;
    world.rate() = 0.5F;
    world.frames(1);
    const float alpha = world.log().alpha;
    CHECK(alpha > 0.4F);
    CHECK(alpha < 0.6F);
    world.rate() = 0.0F;
    CHECK(world.steps_in(10) == 0);
    CHECK(world.log().alpha == alpha);
}

TEST_CASE("rate 4 steps four times a frame, never more than max:", "[runtime][phase-rate]") {
    World world;
    world.rate() = 4.0F;
    CHECK(world.steps_in(1) == 4);
    CHECK(world.steps_in(10) == 40);
    world.rate() = 100.0F;
    CHECK(world.steps_in(1) == 8);
    world.rate() = 1.0e9F;
    CHECK(world.steps_in(1) == 8);
    world.rate() = std::numeric_limits<float>::infinity();
    CHECK(world.steps_in(1) == 8);
    world.rate() = 1.0F;
    CHECK(world.steps_in(1) == 1);
}

TEST_CASE("a NaN or negative rate counts as 0", "[runtime][phase-rate]") {
    World world;
    world.rate() = std::numeric_limits<float>::quiet_NaN();
    CHECK(world.steps_in(10) == 0);
    world.rate() = -1.0F;
    CHECK(world.steps_in(10) == 0);
    world.rate() = 1.0F;
    CHECK(world.steps_in(1) == 1);
    CHECK(rate_faults() == 0);
}

TEST_CASE("a write from the phase's own step applies from the next frame", "[runtime][phase-rate]") {
    World world;
    world.rate()          = 2.0F;
    world.log().stop_at   = 1;
    CHECK(world.steps_in(1) == 2);
    CHECK(world.rate() == 0.0F);
    CHECK(world.steps_in(5) == 0);
}

TEST_CASE("a write from an upstream phase applies in the same frame", "[runtime][phase-rate]") {
    World world;
    world.log().brake = true;
    CHECK(world.steps_in(1) == 0);
    CHECK(world.rate() == 0.0F);
}

TEST_CASE("save and restore keep the rate", "[runtime][phase-rate][persistence]") {
    World world;
    world.rate()        = 0.5F;
    const auto snapshot = backend::generated_capture_world_snapshot(world.registry);

    World restored;
    const auto outcome = backend::generated_restore_world(restored.registry, snapshot);
    INFO(outcome.message);
    REQUIRE(outcome.ok);
    CHECK(restored.rate() == 0.5F);
    CHECK(restored.steps_in(60) == 30);
}

TEST_CASE("a destroyed rate entity stops the phase and reports one fault", "[runtime][phase-rate]") {
    World world;
    world.registry.destroy(World::slots().Gear);
    CHECK(world.steps_in(5) == 0);
    REQUIRE(rate_faults() == 1);
    const auto fault = std::ranges::find(
        backend::runtime_faults(), backend::RangeFaultReason::MissingRateSource, &backend::RangeFault::reason);
    const auto text = backend::describe_range_fault(*fault);
    CHECK(text.contains("phase_rate.Gear"));
    CHECK(text.contains("phase_rate.Speed"));
    CHECK(text.contains("rate"));
    CHECK(text.contains("phase_rate.sim"));
}

TEST_CASE("a removed rate trait stops the phase; losing it again reports again", "[runtime][phase-rate]") {
    World world;
    const auto gear = World::slots().Gear;
    world.registry.remove<game::Speed>(gear);
    CHECK(world.steps_in(5) == 0);
    CHECK(rate_faults() == 1);
    world.registry.emplace<game::Speed>(gear);
    CHECK(world.steps_in(3) == 3);
    CHECK(rate_faults() == 1);
    world.registry.remove<game::Speed>(gear);
    CHECK(world.steps_in(3) == 0);
    CHECK(rate_faults() == 2);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
