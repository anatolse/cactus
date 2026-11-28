// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace {

namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        backend::release_render_resources();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
    }

    void frame() {
        backend::generated_inject_external_event(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        backend::generated_drain_external_events(registry);
    }
};

}  // namespace

TEST_CASE("cover and light: a missing texture is noted once, however often it is drawn",
          "[example-behavior][drawing-rules][textures]") {
    World world;
    const auto before = backend::render_debug_state().missing_assets;
    for (int frame = 0; frame < 3; ++frame) {
        world.frame();
    }
    CHECK(backend::render_debug_state().missing_assets - before == 1);
    const auto& diagnostics = backend::render_debug_state().texture_diagnostics;
    CHECK(std::ranges::any_of(diagnostics, [](const auto& line) { return line.contains("does_not_exist.png"); }));
}

TEST_CASE("cover and light: a draw binds each texture it samples, once per run of rows",
          "[example-behavior][drawing-rules][textures]") {
    World world;
    world.frame();
    const auto before = backend::perf_counters().drawing_textures;
    world.frame();
    // Probes binds its asset once per draw; Tiles binds each of its three rows' textures.
    CHECK(backend::perf_counters().drawing_textures - before == 4);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
