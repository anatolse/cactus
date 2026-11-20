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

namespace game    = cactus_gen::game_pace_runtime;
namespace core    = cactus_gen::stdlib::core;
namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        backend::generated_reset_scheduler_state();
        backend::clear_runtime_faults();
        cactus_headless_test::init_and_load(registry);
    }

    static auto& slots() { return cactus_gen::detail::generated_named_slots(); }

    void frames(int count) {
        for (int frame = 0; frame < count; ++frame) {
            cactus_headless_test::drive_frame(registry);
        }
    }

    [[nodiscard]] game::Counts& counts() { return registry.get<game::Counts>(slots().game_pace_runtime.Meter); }
    [[nodiscard]] static entt::entity clock() { return slots().stdlib.core.Clock; }
    [[nodiscard]] float scale() { return registry.get<core::Pace>(clock()).scale; }

    void request(float scale) {
        counts().request = scale;
        counts().apply   = true;
    }
};

}  // namespace

TEST_CASE("the default pace runs fixed_tick once per frame", "[runtime][game-pace]") {
    World world;
    CHECK(world.scale() == 1.0F);
    world.frames(60);
    CHECK(world.counts().fixed == 60);
    CHECK(world.counts().ticks == 60);
}

TEST_CASE("a hit-stop from fixed_tick stops fixed_tick from the next frame, tick keeps running",
          "[runtime][game-pace]") {
    World world;
    world.counts().stop_at = 3;
    world.frames(3);
    CHECK(world.counts().fixed == 3);
    CHECK(world.scale() == 0.0F);
    world.frames(10);
    CHECK(world.counts().fixed == 3);
    CHECK(world.counts().ticks == 13);
}

TEST_CASE("resuming the pace does not catch up on the stopped time", "[runtime][game-pace]") {
    World world;
    world.request(0.0F);
    world.frames(30);
    CHECK(world.counts().fixed == 0);
    world.request(1.0F);
    world.frames(1);
    CHECK(world.counts().fixed == 1);
}

TEST_CASE("an out-of-range pace reads back inside its bounds", "[runtime][game-pace]") {
    World world;
    world.request(-1.0F);
    world.frames(1);
    CHECK(world.counts().seen == 0.0F);
    world.request(5000.0F);
    world.frames(1);
    CHECK(world.counts().seen == 1024.0F);
}

TEST_CASE("the clock survives scene cleanup and keeps its pace", "[runtime][game-pace]") {
    World world;
    world.request(0.5F);
    world.frames(1);
    cactus_headless_test::dispatch_unload_event(world.registry);
    REQUIRE(world.registry.valid(world.clock()));
    CHECK(world.scale() == 0.5F);
    const int before = world.counts().fixed;
    world.frames(60);
    CHECK(world.counts().fixed - before == 30);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
