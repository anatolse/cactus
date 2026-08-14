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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

}  // namespace

TEST_CASE("refactor-bouncy-bubbles-colliders: every wall, including the tilted one, keeps bubbles contained",
          "[runtime][codegen-entt][example][bouncy-bubbles]") {
    // The arena is x:[150,650] y:[50,550] with 20-unit-thick walls (see
    // main.cactus's "Entities: arena walls" comment); RightWall's collider is
    // additionally tilted 3 degrees off its cardinal 90-degree orientation.
    // A generous containment margin absorbs the corner-gap/discrete-tunneling
    // slack the design doc calls out (~14 world units at the tilted corner)
    // without being so loose it would miss a genuine escape.
    constexpr float kMinX     = 150.0F - 40.0F;
    constexpr float kMaxX     = 650.0F + 40.0F;
    constexpr float kMinY     = 50.0F - 40.0F;
    constexpr float kMaxY     = 550.0F + 40.0F;
    constexpr int kFrameCount = 900;  // 15 simulated seconds at 60 fps

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto wall_view = registry.view<main__Wall, main__SquareCollider, std_transform_flat__WorldTransform>();
    std::size_t wall_count = 0;
    bool saw_tilted_wall   = false;
    for (auto entity : wall_view) {
        ++wall_count;
        const float rotation = registry.get<std_transform_flat__WorldTransform>(entity).rotation;
        const float mod90    = std::fmod(std::fmod(rotation, std::numbers::pi_v<float> / 2.0F) +
                                           (std::numbers::pi_v<float> / 2.0F),
                                       std::numbers::pi_v<float> / 2.0F);
        if (mod90 > 0.01F && mod90 < (std::numbers::pi_v<float> / 2.0F) - 0.01F) {
            saw_tilted_wall = true;
        }
    }
    REQUIRE(wall_count == 4);
    CHECK(saw_tilted_wall);

    const auto bubble_view =
        registry.view<main__Bubble, main__CircleCollider, std_transform_flat__WorldTransform>();
    std::size_t bubble_count = 0;
    for (auto entity : bubble_view) {
        (void)entity;
        ++bubble_count;
    }
    REQUIRE(bubble_count == 6);

    for (int frame = 0; frame < kFrameCount; ++frame) {
        drive_frame(registry);
        for (auto entity : bubble_view) {
            const auto& position = registry.get<std_transform_flat__WorldTransform>(entity).position;
            INFO("frame " << frame << " entity " << static_cast<std::uint32_t>(entity) << " position ("
                           << position.x << ", " << position.y << ")");
            CHECK(position.x >= kMinX);
            CHECK(position.x <= kMaxX);
            CHECK(position.y >= kMinY);
            CHECK(position.y <= kMaxY);
        }
    }
}

TEST_CASE("refactor-bouncy-bubbles-colliders: wall Shape.origin pivots rendering at the collider's own center",
          "[runtime][codegen-entt][example][bouncy-bubbles][render]") {
    // WallTemplate sets shapes.Shape.origin = (270, 10) = size/2, so
    // ShapeRenderer must draw every wall as a 540x20 rectangle rotated around
    // its own WorldTransform.position — matching what SquareCollider already
    // assumes — instead of raylib's default top-left-corner pivot. This is
    // the fix for the collider/render origin mismatch the oriented-box
    // collision math would otherwise have relative to the old
    // top-left-anchored DrawRectangleV render call.
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    struct ExpectedWall {
        Vector2 position;
        float rotation_deg;
    };
    constexpr float kRadToDeg = 180.0F / std::numbers::pi_v<float>;
    const std::array<ExpectedWall, 4> expected_walls{{
        {.position = {.x = 400.0F, .y = 40.0F}, .rotation_deg = 0.0F},
        {.position = {.x = 400.0F, .y = 560.0F}, .rotation_deg = 3.14159265F * kRadToDeg},
        {.position = {.x = 140.0F, .y = 300.0F}, .rotation_deg = 4.71238898F * kRadToDeg},
        {.position = {.x = 660.0F, .y = 300.0F}, .rotation_deg = 1.62315620F * kRadToDeg},
    }};

    for (const auto& wall : expected_walls) {
        const cactus_raylib_fake::RecordedDrawRectanglePro* match = nullptr;
        for (const auto& entry : cactus_raylib_fake::call_log()) {
            const auto* draw = std::get_if<cactus_raylib_fake::RecordedDrawRectanglePro>(&entry);
            if (draw != nullptr && cactus_raylib_fake::approx_equal(Vector2{.x = draw->rec.x, .y = draw->rec.y},
                                                                     wall.position)) {
                match = draw;
                break;
            }
        }
        INFO("wall at (" << wall.position.x << ", " << wall.position.y << ")");
        REQUIRE(match != nullptr);
        CHECK(match->rec.width == Catch::Approx(540.0F));
        CHECK(match->rec.height == Catch::Approx(20.0F));
        CHECK(cactus_raylib_fake::approx_equal(match->origin, Vector2{.x = 270.0F, .y = 10.0F}));
        CHECK(match->rotation == Catch::Approx(wall.rotation_deg).margin(0.01));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
