// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_VISUAL_GENERATED_CPP
#error "CACTUS_VISUAL_GENERATED_CPP must name the generated translation unit"
#endif
#ifndef CACTUS_VISUAL_TEST_OUTPUT_DIR
#error "CACTUS_VISUAL_TEST_OUTPUT_DIR must name a directory relative to the test's working directory"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_VISUAL_GENERATED_CPP

#include "pixel_inspection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <raylib.h>

#include <string>

namespace {

// examples/render-pass-domain/render_pass_domain.cactus places one entity per column on ROW_Y.
constexpr int kRowY       = 300;
constexpr int kYoungX     = 100;
constexpr int kBareX      = 250;
constexpr int kConcealedX = 400;
constexpr int kOldX       = 550;
constexpr int kOverlapX   = 700;
constexpr unsigned char kTolerance = 40;

Color pixel_at(const Image& image, int x) {
    return GetImageColor(image, x, kRowY);
}

}  // namespace

TEST_CASE("render-pass-domain visual: only the vertex handler's domain is drawn, in order-by order",
          "[example-behavior][render-pass-domain][render-passes][visual]") {
    const auto config = cactus::runtime::entt_backend::generated_project_config();
    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    // Two frames: see test_particle_burst_visual.cpp for why one is not enough.
    constexpr int kFramesBeforeScreenshot = 2;
    for (int frame = 0; frame < kFramesBeforeScreenshot; ++frame) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        cactus::runtime::entt_backend::generated_inject_external_event(
            cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
        EndDrawing();
    }

    const std::string screenshot_path =
        std::string(CACTUS_VISUAL_TEST_OUTPUT_DIR) + "/render_pass_domain_visual_test.png";
    TakeScreenshot(screenshot_path.c_str());
    CloseWindow();

    const Image screenshot = LoadImage(screenshot_path.c_str());
    REQUIRE(screenshot.data != nullptr);
    const Color young     = pixel_at(screenshot, kYoungX);
    const Color bare      = pixel_at(screenshot, kBareX);
    const Color concealed = pixel_at(screenshot, kConcealedX);
    const Color old       = pixel_at(screenshot, kOldX);
    const Color overlap   = pixel_at(screenshot, kOverlapX);
    UnloadImage(screenshot);

    CHECK(cactus_visual_test::colors_close(young, Color{.r = 0, .g = 255, .b = 0, .a = 255}, kTolerance));
    CHECK(cactus_visual_test::colors_close(bare, RAYWHITE, kTolerance));
    CHECK(cactus_visual_test::colors_close(concealed, RAYWHITE, kTolerance));
    CHECK(cactus_visual_test::colors_close(old, RAYWHITE, kTolerance));
    // Back (red, depth 2) is drawn after Front (blue, depth 1), though Front is newer.
    CHECK(cactus_visual_test::colors_close(overlap, Color{.r = 255, .g = 0, .b = 0, .a = 255}, kTolerance));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
