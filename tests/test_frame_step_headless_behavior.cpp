// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

namespace {

namespace game = cactus_gen::frame_step_runtime;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    void frame(float dt) {
        cactus_headless_test::drive_frame(registry, dt);
    }

    void inject(float dt, std::int64_t delta_units) {
        cactus::runtime::entt_backend::generated_inject_external_event(
            cactus_gen::stdlib::core::frameEvent{.dt = dt, .delta = cactus::Duration{delta_units}});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
    }

    [[nodiscard]] const game::Seen& seen() {
        return registry.get<game::Seen>(cactus_gen::detail::generated_named_slots().frame_step_runtime.Probe);
    }
};

}  // namespace

TEST_CASE("an ordinary step is unchanged", "[runtime][frame-step]") {
    World world;
    world.frame(0.016F);
    CHECK(world.seen().dt == 0.016F);
    CHECK(world.seen().delta == cactus::duration_from_seconds(0.016F));
}

TEST_CASE("a negative step becomes zero", "[runtime][frame-step]") {
    World world;
    world.frame(-0.5F);
    CHECK(world.seen().dt == 0.0F);
    CHECK(world.seen().delta.units == 0);
    CHECK(world.seen().fixed_runs == 0);
}

TEST_CASE("a NaN or infinite step becomes zero", "[runtime][frame-step]") {
    for (const float dt : {std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity()}) {
        World world;
        world.frame(dt);
        CHECK(world.seen().dt == 0.0F);
        CHECK(world.seen().delta.units == 0);
        CHECK(world.seen().fixed_runs == 0);
    }
}

TEST_CASE("a host's negative delta becomes zero", "[runtime][frame-step]") {
    World world;
    world.inject(0.016F, -5);
    CHECK(world.seen().dt == 0.016F);
    CHECK(world.seen().delta.units == 0);
}

TEST_CASE("a bad step does not move the periodic clock", "[runtime][frame-step]") {
    World world;
    world.frame(-0.5F);
    world.frame(1.0F / 60.0F);
    CHECK(world.seen().fixed_runs == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
