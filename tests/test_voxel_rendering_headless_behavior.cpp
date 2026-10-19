// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

namespace backend = cactus::runtime::entt_backend;

using Cell  = std_voxel_volume__Cell;
using Solid = std_physics_volume__Solid;

// Faces of the fixture's terrain. Stone slab 4 x 1 x 4 under a 2 x 1 x 4 wood
// block: stone shows 8 uncovered tops, 16 bottoms and 16 sides; wood shows 8
// tops and 12 sides.
constexpr int kFullFaces = 60;
// Cut above layer -1: stone alone, with all 16 tops.
constexpr int kCutFaces = 48;

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
            backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
            backend::generated_drain_external_events(registry);
        }
    }

    entt::entity cell_at(int x, int y, int z) {
        for (const auto [entity, cell] : registry.view<Cell>().each()) {
            if (cell.x == x && cell.y == y && cell.z == z) {
                return entity;
            }
        }
        return entt::null;
    }

    static const backend::RenderDebugState& debug() {
        return backend::render_debug_state();
    }
};

}  // namespace

TEST_CASE("both cameras draw the exposed faces, the cut one only below its layer",
          "[runtime][codegen-entt][stdlib-voxel-volume][render]") {
    World world;
    world.frame();
    CHECK(World::debug().voxel_faces_drawn == kFullFaces + kCutFaces);
    // Stone and wood meshes for the full view, stone alone for the cut one.
    CHECK(World::debug().voxel_draw_calls == 3);
}

TEST_CASE("an unchanged frame reuses every chunk mesh", "[runtime][codegen-entt][stdlib-voxel-volume][render]") {
    World world;
    world.frame();
    const int builds = World::debug().voxel_chunk_builds;
    world.frame(5);
    CHECK(World::debug().voxel_chunk_builds == builds);
    CHECK(World::debug().voxel_faces_drawn == kFullFaces + kCutFaces);
}

TEST_CASE("digging exposes the faces behind the removed cell", "[runtime][codegen-entt][stdlib-voxel-volume][render]") {
    World world;
    world.frame();
    const int builds = World::debug().voxel_chunk_builds;
    // A wood cell on the block's +X edge shows its top and +X faces. Removing it
    // exposes the three wood cells beside it and the stone below.
    world.registry.remove<Solid>(world.cell_at(1, 0, 1));
    world.frame();
    CHECK(World::debug().voxel_chunk_builds > builds);
    CHECK(World::debug().voxel_faces_drawn == (kFullFaces - 2 + 4) + kCutFaces);
}

TEST_CASE("a texture write changes the material without changing faces",
          "[runtime][codegen-entt][stdlib-voxel-volume][render]") {
    World world;
    world.frame();
    world.registry.get<Cell>(world.cell_at(3, -1, 3)).texture = Wood;
    world.frame();
    CHECK(World::debug().voxel_faces_drawn == kFullFaces + kCutFaces);
    // The cut view now needs a wood mesh too.
    CHECK(World::debug().voxel_draw_calls == 4);
}

TEST_CASE("a replaced world rebuilds the surfaces", "[runtime][codegen-entt][stdlib-voxel-volume][render]") {
    World world;
    world.frame();
    const int builds = World::debug().voxel_chunk_builds;
    backend::advance_binding_index_epoch();
    world.frame();
    CHECK(World::debug().voxel_chunk_builds > builds);
    CHECK(World::debug().voxel_faces_drawn == kFullFaces + kCutFaces);
}

TEST_CASE("the cutaway changes no solid cell", "[runtime][codegen-entt][stdlib-voxel-volume][render]") {
    World world;
    world.frame();
    CHECK(world.registry.all_of<Solid>(world.cell_at(0, 0, 0)));
    CHECK(world.registry.all_of<Solid>(world.cell_at(1, 0, 3)));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
