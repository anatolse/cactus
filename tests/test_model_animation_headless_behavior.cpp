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

#include <algorithm>
#include <array>
#include <vector>

namespace {

// examples/model-animation/main.cactus: SpawnCharacters (on load) places
// robot/knight/robot at (-2.5,0,0), (0,0,0), (2.5,0,0). Positions never
// change afterward — RotateSelected only rotates the selected character,
// AnimateSelection only rescales it — so every driven frame should still
// attribute a DrawMesh to each of these three world positions.
constexpr std::array<Vector3, 3> kCharacterPositions{
    Vector3{.x = -2.5F, .y = 0.0F, .z = 0.0F},
    Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
    Vector3{.x = 2.5F, .y = 0.0F, .z = 0.0F},
};
constexpr float kDt = 0.1F;

}  // namespace

TEST_CASE("model-animation headless: dynamically spawned character draws appear across multiple frames",
          "[example-behavior][model-animation]") {
    cactus_raylib_fake::reset();
    // Faked model resource loading (see runtime.cpp's fake_model_resource)
    // needs the window "ready" to materialize at all, same as mesh-renderer.
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;
    constexpr int kFrameCount = 3;
    cactus_headless_test::drive_frames(registry, kDt, kFrameCount);

    // Partition the log into per-frame 3D segments (the entries strictly
    // between each BeginMode3D/EndMode3D pair) so a regression that only
    // drops a character's draw on frame 2+ is caught, not just frame 1.
    std::vector<std::vector<Matrix>> per_frame_mesh_transforms;
    bool in_3d = false;
    for (const auto& entry : cactus_raylib_fake::call_log()) {
        if (std::holds_alternative<cactus_raylib_fake::RecordedBeginMode3D>(entry)) {
            in_3d = true;
            per_frame_mesh_transforms.emplace_back();
            continue;
        }
        if (std::holds_alternative<cactus_raylib_fake::RecordedEndMode3D>(entry)) {
            in_3d = false;
            continue;
        }
        if (in_3d) {
            if (const auto* draw = std::get_if<cactus_raylib_fake::RecordedDrawMesh>(&entry)) {
                per_frame_mesh_transforms.back().push_back(draw->transform);
            }
        }
    }

    REQUIRE(per_frame_mesh_transforms.size() == static_cast<std::size_t>(kFrameCount));
    for (const auto& frame_transforms : per_frame_mesh_transforms) {
        for (const Vector3 expected_position : kCharacterPositions) {
            const bool attributed =
                std::ranges::any_of(frame_transforms, [&](const Matrix& transform) {
                    return cactus_raylib_fake::approx_equal(transform, expected_position);
                });
            CHECK(attributed);
        }
    }
}

TEST_CASE("model-animation headless: window-space HUD content is recorded after per-viewport 3D content",
          "[example-behavior][model-animation]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kDt);

    const auto& log = cactus_raylib_fake::call_log();
    CHECK(cactus_raylib_fake::ordered_subsequence(
        log, {cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedBeginMode3D>(),
              cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedDrawMesh>(),
              cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedEndMode3D>(),
              cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedDrawTextEx>()}));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
