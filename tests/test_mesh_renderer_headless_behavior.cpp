// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

// examples/mesh-renderer/main.cactus: BlueCube at (0,0,0), BlueCube2 at
// (3,1,0). No camera entity is declared, so the queued mesh draw path uses
// the runtime's default 3D camera (get_active_camera_3d()).
constexpr Vector3 kBlueCubePosition{.x = 0.0F, .y = 0.0F, .z = 0.0F};
constexpr Vector3 kBlueCube2Position{.x = 3.0F, .y = 1.0F, .z = 0.0F};
constexpr float kDt = 0.1F;

}  // namespace

TEST_CASE("mesh-renderer headless: queued mesh draws for all entities appear in the call log after flush",
          "[example-behavior][mesh-renderer]") {
    cactus_raylib_fake::reset();
    // The queued mesh-draw flush (flush_mesh_queue) only materializes fake
    // GPU resources and issues Begin/EndMode3D + DrawMesh when the window is
    // "ready" — unlike blue-square's immediate shape-draw path, which needs
    // no window at all. Asset materialization (Gen*/Load*/UploadMesh) is
    // deliberately left unwrapped by the fake (see raylib_io.hpp), but with
    // no scripted key/mouse input and no real GL calls beyond mesh/material
    // creation, this is safe headlessly.
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kDt);

    const auto& log = cactus_raylib_fake::call_log();
    const auto* blue_cube_draw =
        cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawMesh>(log, [](const auto& draw) {
            return cactus_raylib_fake::approx_equal(draw.transform, kBlueCubePosition);
        });
    const auto* blue_cube2_draw =
        cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawMesh>(log, [](const auto& draw) {
            return cactus_raylib_fake::approx_equal(draw.transform, kBlueCube2Position);
        });

    CHECK(blue_cube_draw != nullptr);
    CHECK(blue_cube2_draw != nullptr);
}

TEST_CASE("mesh-renderer headless: flushed mesh draws are bracketed by the viewport's 3D mode calls",
          "[example-behavior][mesh-renderer]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kDt);

    const auto& log = cactus_raylib_fake::call_log();
    CHECK(cactus_raylib_fake::ordered_subsequence(
        log, {cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedBeginMode3D>(),
              cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedDrawMesh>(),
              cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedDrawMesh>(),
              cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedEndMode3D>()}));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
