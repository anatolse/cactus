// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace {

namespace backend = cactus::runtime::entt_backend;

using Flipbook = cactus_gen::stdlib::voxel::volume::TextureFlipbook;

struct World {
    entt::registry registry;

    World() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        cactus_raylib_fake::set_screen_size(800, 400);
        backend::release_render_resources();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
        frame(3);
    }

    void frame(int count = 1) {
        for (int i = 0; i < count; ++i) {
            backend::generated_inject_external_event(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
            backend::generated_drain_external_events(registry);
        }
    }

    static const backend::RenderDebugState& debug() {
        return backend::render_debug_state();
    }

    // The (frame, frames) each voxel draw call used this frame.
    static bool drew(std::pair<int, int> frame) {
        return std::ranges::contains(debug().voxel_draw_frames, frame);
    }

    static bool diagnosed(const std::string& text) {
        return std::ranges::any_of(debug().texture_diagnostics,
                                   [&](const std::string& message) { return message.contains(text); });
    }
};

TEST_CASE("Flipbook frame selection follows presentation time", "[runtime][stdlib-voxel-volume][flipbook]") {
    CHECK(backend::flipbook_frame(1.0, 4.0F, 8) == 4);
    CHECK(backend::flipbook_frame(2.5, 4.0F, 8) == 2);
    CHECK(backend::flipbook_frame(0.0, 4.0F, 8) == 0);
    CHECK(backend::flipbook_frame(3.0, 0.0F, 8) == 0);
    CHECK(backend::flipbook_frame_count(64, 512) == 8);
    CHECK(backend::flipbook_frame_count(64, 64) == 1);
    CHECK_FALSE(backend::flipbook_frame_count(64, 100).has_value());
    CHECK_FALSE(backend::flipbook_frame_count(64, 0).has_value());
}

TEST_CASE("Lava faces show the frame for the presentation time", "[runtime][codegen-entt][stdlib-voxel-volume][flipbook]") {
    World world;
    cactus_raylib_fake::set_time(1.0);
    world.frame();
    CHECK(World::drew({4, 8}));
    CHECK(World::drew({0, 1}));
    cactus_raylib_fake::set_time(2.5);
    world.frame();
    CHECK(World::drew({2, 8}));
}

TEST_CASE("A flipbook frame change rebuilds no surface", "[runtime][codegen-entt][stdlib-voxel-volume][flipbook]") {
    World world;
    cactus_raylib_fake::set_time(0.1);
    world.frame();
    const auto builds = World::debug().voxel_chunk_builds;
    cactus_raylib_fake::set_time(0.6);
    world.frame();
    CHECK(World::drew({2, 8}));
    CHECK(World::debug().voxel_chunk_builds == builds);
}

TEST_CASE("A texture with no flipbook draws as a still texture", "[runtime][codegen-entt][stdlib-voxel-volume][flipbook]") {
    World world;
    cactus_raylib_fake::set_time(1.75);
    world.frame();
    CHECK(World::drew({0, 1}));
    CHECK(std::ranges::all_of(World::debug().voxel_draw_frames,
                              [](const auto& frame) { return frame == std::pair{0, 1} || frame.second == 8; }));
}

TEST_CASE("A strip that is not square frames fails to load, naming the asset",
          "[runtime][codegen-entt][stdlib-voxel-volume][flipbook]") {
    World world;
    CHECK(World::diagnosed("bad_strip.png"));
}

TEST_CASE("Two flipbooks for one texture are an error naming it", "[runtime][codegen-entt][stdlib-voxel-volume][flipbook]") {
    World world;
    const auto lava = world.registry.get<Flipbook>(cactus_gen::detail::generated_named_slots().texture_flipbook.LavaFlow);
    const auto copy = backend::create_entity(world.registry);
    world.registry.emplace<Flipbook>(copy, lava);
    world.frame();
    CHECK(World::diagnosed("flipbook_strip.png"));
}

}  // namespace
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
