// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <variant>
#include <vector>

namespace {

namespace backend = cactus::runtime::entt_backend;

using Cell           = cactus_gen::stdlib::voxel::volume::Cell;
using Volume         = cactus_gen::stdlib::voxel::volume::Volume;
using VolumeRenderer = cactus_gen::stdlib::voxel::volume::VolumeRenderer;
using SurfaceStyle   = cactus_gen::stdlib::voxel::volume::SurfaceStyle;
using RayProbe       = cactus_gen::stdlib::voxel::volume::RayProbe;
using Solid          = cactus_gen::stdlib::physics::volume::Solid;
using WorldTransform = cactus_gen::stdlib::transform::volume::WorldTransform;
using Tunnel         = cactus_gen::smooth_volume::Tunnel;
using StorageControl = cactus_gen::smooth_volume::StorageControl;

const auto& slots() {
    return cactus_gen::detail::generated_named_slots();
}

// The vertices one camera drew this frame, in draw order.
using Vertices = std::vector<float>;

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

    // Runs one frame and returns each camera's drawn voxel vertices.
    std::vector<Vertices> draw() {
        const auto start = cactus_raylib_fake::call_log().size();
        frame();
        const auto& log = cactus_raylib_fake::call_log();
        std::vector<Vertices> views;
        for (auto at = start; at < log.size(); ++at) {
            if (std::holds_alternative<cactus_raylib_fake::RecordedBeginMode3D>(log[at])) {
                views.emplace_back();
            }
            if (const auto* mesh = std::get_if<cactus_raylib_fake::RecordedDrawMesh>(&log[at]);
                mesh != nullptr && !views.empty()) {
                const auto count = static_cast<std::size_t>(mesh->mesh.vertexCount) * 3U;
                views.back().insert(views.back().end(), mesh->mesh.vertices, mesh->mesh.vertices + count);
            }
        }
        std::erase_if(views, [](const Vertices& view) { return view.empty(); });
        return views;
    }

    entt::entity rock() {
        for (const auto [entity, volume] : registry.view<Volume>().each()) {
            return entity;
        }
        return entt::null;
    }

    VolumeRenderer& renderer() {
        return registry.get<VolumeRenderer>(rock());
    }

    entt::entity cell_at(int x, int y, int z) {
        for (const auto [entity, cell] : registry.view<Cell>().each()) {
            if (cell.x == x && cell.y == y && cell.z == z) {
                return entity;
            }
        }
        return entt::null;
    }

    void dig(int x, int y, int z) {
        registry.remove<Solid>(cell_at(x, y, z));
    }

    static int builds() {
        return backend::render_debug_state().voxel_chunk_builds;
    }

    static int debug_faces() {
        return backend::render_debug_state().voxel_faces_drawn;
    }
};

float max_y(const Vertices& view) {
    float high = -1000.0F;
    for (std::size_t at = 1; at < view.size(); at += 3) {
        high = std::max(high, view[at]);
    }
    return high;
}

}  // namespace

TEST_CASE("a smooth volume draws a rounded surface off the cell grid",
          "[runtime][codegen-entt][stdlib-voxel-volume][render][smooth]") {
    World world;
    const auto views = world.draw();
    REQUIRE(views.size() == 2);
    // Blocky corners all sit on whole cell coordinates; rounded ones do not.
    CHECK(std::ranges::any_of(views[0], [](float value) { return value != std::floor(value); }));
}

TEST_CASE("switching the style rebuilds the surface", "[runtime][codegen-entt][stdlib-voxel-volume][render][smooth]") {
    World world;
    const auto smooth = world.draw();
    const int faces   = World::debug_faces();
    const int builds  = World::builds();
    world.renderer().style = SurfaceStyle::Blocky;
    const auto blocky      = world.draw();
    CHECK(World::builds() > builds);
    CHECK(blocky != smooth);
    CHECK(World::debug_faces() == faces);
    CHECK(std::ranges::all_of(blocky[0], [](float value) { return value == std::floor(value); }));
    world.renderer().style = SurfaceStyle::Smooth;
    CHECK(world.draw() == smooth);
}

TEST_CASE("a seed or roughness change rebuilds the surface", "[runtime][codegen-entt][stdlib-voxel-volume][render][smooth]") {
    World world;
    const auto first = world.draw();
    world.renderer().seed = 8;
    const auto reseeded = world.draw();
    CHECK(reseeded != first);
    world.renderer().roughness = 0.0F;
    CHECK(world.draw() != reseeded);
}

TEST_CASE("the cutaway hides the smooth surface above its layer",
          "[runtime][codegen-entt][stdlib-voxel-volume][render][smooth]") {
    World world;
    const auto views = world.draw();
    REQUIRE(views.size() == 2);
    const float full = std::max(max_y(views[0]), max_y(views[1]));
    const float cut  = std::min(max_y(views[0]), max_y(views[1]));
    // Rock top at y = 5; the cut closes at the top of layer 2, y = 3.
    CHECK(full > 4.5F);
    CHECK(cut <= 3.0F + 0.2F + 1e-4F);
    CHECK(cut > 2.5F);
}

TEST_CASE("an isolated dig rebuilds only the chunk around it", "[runtime][codegen-entt][stdlib-voxel-volume][render][smooth]") {
    World world;
    (void)world.draw();
    int builds = World::builds();
    // x = 8 is two cells inside chunk 0; each camera rebuilds that one chunk.
    world.dig(8, 2, 2);
    (void)world.draw();
    CHECK(World::builds() == builds + 2);
    builds = World::builds();
    // x = 15 borders chunk 1, so both chunks rebuild for both cameras.
    world.dig(15, 2, 2);
    (void)world.draw();
    CHECK(World::builds() == builds + 4);
}

TEST_CASE("a tunnel dug in any order and pace draws the same surface",
          "[runtime][codegen-entt][stdlib-voxel-volume][render][smooth]") {
    Vertices dug_by_rule;
    {
        World world;
        world.registry.get<Tunnel>(slots().smooth_volume.Digger).active = true;
        world.frame(20);
        CHECK(world.registry.get<Tunnel>(slots().smooth_volume.Digger).next_x == 18);
        CHECK_FALSE(world.registry.all_of<Solid>(world.cell_at(10, 2, 2)));
        dug_by_rule = world.draw()[0];
    }
    World world;
    for (int x = 17; x >= 2; x -= 2) {
        world.dig(x, 2, 2);
        world.dig(x - 1, 2, 2);
        (void)world.draw();
    }
    CHECK(world.draw()[0] == dug_by_rule);
}

TEST_CASE("a restored smooth volume draws the saved vertices",
          "[runtime][codegen-entt][stdlib-voxel-volume][render][smooth][persistence]") {
    cactus_test::InMemoryPersistenceAdapter memory;
    cactus_test::ScopedPersistenceAdapter scoped(memory.as_adapter());
    World world;
    world.dig(4, 2, 2);
    world.dig(5, 3, 2);
    world.registry.get<StorageControl>(slots().smooth_volume.Storage).save = true;
    world.frame(2);
    REQUIRE(world.registry.get<StorageControl>(slots().smooth_volume.Storage).saved);
    const auto saved = world.draw();

    world.dig(12, 2, 2);
    world.registry.emplace<Solid>(world.cell_at(4, 2, 2));
    world.registry.get<StorageControl>(slots().smooth_volume.Storage).restore = true;
    world.frame(3);
    REQUIRE(world.registry.get<StorageControl>(slots().smooth_volume.Storage).restores == 1);
    REQUIRE(world.renderer().style == SurfaceStyle::Smooth);
    CHECK(world.draw() == saved);
}

TEST_CASE("collision and probes still see whole cells on a smooth volume",
          "[runtime][codegen-entt][stdlib-voxel-volume][smooth]") {
    World world;
    world.frame(90);
    // A 2.0-high capsule rests with its centre 1.0 above the rock's box top.
    CHECK(world.registry.get<WorldTransform>(slots().smooth_volume.Body).position.y == Catch::Approx(6.0F).margin(0.01F));
    // The walker's capsule stops one radius before the wall's box face at x = 0.
    CHECK(world.registry.get<WorldTransform>(slots().smooth_volume.Walker).position.x ==
          Catch::Approx(-0.4F).margin(0.01F));
    const auto& eye = world.registry.get<RayProbe>(slots().smooth_volume.Eye);
    REQUIRE(eye.hit);
    CHECK(eye.point.y == Catch::Approx(5.0F));
    CHECK(eye.y == 4);
    CHECK(world.registry.all_of<Solid>(world.cell_at(10, 4, 0)));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
