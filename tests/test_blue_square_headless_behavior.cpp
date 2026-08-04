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

#include <variant>
#include <vector>

namespace {

// examples/blue-square/square.cactus: Square starts at (375, 275), 50x50,
// color #6495EDFF, SPEED = 200.0, MoveX bound to A(-)/D(+).
constexpr Vector2 kStartPosition{.x = 375.0F, .y = 275.0F};
constexpr Vector2 kShapeSize{.x = 50.0F, .y = 50.0F};
constexpr Color kShapeColor{.r = 0x64, .g = 0x95, .b = 0xED, .a = 0xFF};
constexpr float kSpeed = 200.0F;
constexpr float kDt    = 0.1F;

}  // namespace

TEST_CASE("blue-square headless: scripted MoveX input moves the square and is observed in the draw call",
          "[example-behavior][blue-square]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_key_down(KEY_D, true);

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kDt);

    const auto& log = cactus_raylib_fake::call_log();
    const auto* draw = cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleV>(log);
    REQUIRE(draw != nullptr);

    const Vector2 expected_position{.x = kStartPosition.x + (kSpeed * kDt), .y = kStartPosition.y};
    CHECK(cactus_raylib_fake::approx_equal(draw->position, expected_position));
    CHECK(cactus_raylib_fake::approx_equal(draw->size, kShapeSize));
    CHECK(cactus_raylib_fake::colors_equal(draw->color, kShapeColor));

    // The draw must land between the shape renderer's BeginMode2D/EndMode2D
    // pair — this is the ungated draw-call gap the change targets.
    CHECK(cactus_raylib_fake::ordered_subsequence(
        log, {cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedBeginMode2D>(),
              cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedDrawRectangleV>(),
              cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedEndMode2D>()}));
}

TEST_CASE("blue-square headless: no scripted input leaves the square at its starting position",
          "[example-behavior][blue-square]") {
    // Guards against a test that would pass regardless of whether input is
    // actually wired up: with nothing scripted, the square must not move.
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kDt);

    const auto* draw = cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleV>(
        cactus_raylib_fake::call_log());
    REQUIRE(draw != nullptr);
    CHECK(cactus_raylib_fake::approx_equal(draw->position, kStartPosition));
}

TEST_CASE("blue-square headless: repeated scripted MoveX input across multiple frames accumulates movement",
          "[example-behavior][blue-square]") {
    // Guards against frame-event delivery bugs that only manifest on the
    // second-plus tick (queued/duplicated/dropped events) — blue-square's
    // other two scenarios only ever drive a single frame.
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_key_down(KEY_D, true);

    entt::registry registry;
    constexpr int kFrameCount = 4;
    cactus_headless_test::drive_frames(registry, kDt, kFrameCount);

    std::vector<Vector2> draw_positions;
    for (const auto& entry : cactus_raylib_fake::call_log()) {
        if (const auto* draw = std::get_if<cactus_raylib_fake::RecordedDrawRectangleV>(&entry)) {
            draw_positions.push_back(draw->position);
        }
    }
    REQUIRE(draw_positions.size() == static_cast<std::size_t>(kFrameCount));

    for (int frame = 0; frame < kFrameCount; ++frame) {
        const Vector2 expected_position{
            .x = kStartPosition.x + (kSpeed * kDt * static_cast<float>(frame + 1)), .y = kStartPosition.y};
        CHECK(cactus_raylib_fake::approx_equal(draw_positions[static_cast<std::size_t>(frame)], expected_position));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
