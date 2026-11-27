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

#include <algorithm>
#include <string>

namespace {

// Places in examples/drawing_rules.cactus, in window pixels while the camera is at rest.
constexpr int kHotX       = 190;  // inside the Hot dot, off its caption
constexpr int kHotY       = 190;
constexpr int kCoolX      = 400;
constexpr int kCoolY      = 200;
constexpr int kSinkX      = 600;
constexpr int kSinkY      = 200;
constexpr int kBadgeX     = 740;  // the screen size less the badge margin
constexpr int kBadgeY     = 540;
constexpr int kCameraStep = 100;
constexpr int kHazeY      = 470;  // a row through the haze patch, x from 40 to 220
constexpr int kHazeX0     = 50;
constexpr int kHazeX1     = 210;

struct Rect {
    int x0;
    int y0;
    int x1;
    int y1;
};

constexpr Rect kHotCaption{.x0 = 205, .y0 = 205, .x1 = 240, .y1 = 240};
constexpr Rect kCornerCaption{.x0 = 700, .y0 = 520, .x1 = 745, .y1 = 560};

bool is_ink(Color pixel) {
    return pixel.r < 60 && pixel.g < 60 && pixel.b < 60;
}

bool has_ink(const Image& image, Rect rect) {
    for (int y = rect.y0; y < rect.y1; ++y) {
        for (int x = rect.x0; x < rect.x1; ++x) {
            if (is_ink(GetImageColor(image, x, y))) {
                return true;
            }
        }
    }
    return false;
}

// Gray everywhere along the row, but not one flat shade: the fragment stage computed noise.
bool shows_noise(const Image& image) {
    int lowest  = 255;
    int highest = 0;
    for (int x = kHazeX0; x <= kHazeX1; x += 4) {
        const Color pixel = GetImageColor(image, x, kHazeY);
        if (pixel.r != pixel.g || pixel.g != pixel.b) {
            return false;
        }
        lowest  = std::min(lowest, static_cast<int>(pixel.r));
        highest = std::max(highest, static_cast<int>(pixel.r));
    }
    return highest - lowest > 30;
}

bool reddish(Color pixel) {
    return pixel.r > pixel.b + 60 && pixel.r > pixel.g + 60;
}

bool bluish(Color pixel) {
    return pixel.b > pixel.r + 60 && pixel.b > pixel.g + 60;
}

void run_frames(entt::registry& registry, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        cactus::runtime::entt_backend::generated_inject_external_event(
            cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
        EndDrawing();
    }
}

Image screenshot(const char* name) {
    const std::string path = std::string(CACTUS_VISUAL_TEST_OUTPUT_DIR) + "/" + name;
    TakeScreenshot(path.c_str());
    return LoadImage(path.c_str());
}

}  // namespace

TEST_CASE("drawing-rules visual: rules draw in their layers, in world or screen space",
          "[example-behavior][drawing-rules][visual]") {
    const auto config = cactus::runtime::entt_backend::generated_project_config();
    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    // Two frames: see test_particle_burst_visual.cpp for why one is not enough.
    run_frames(registry, 2);
    const Image at_rest = screenshot("drawing_rules_visual_at_rest.png");

    auto& camera = registry.get<cactus_gen::stdlib::camera::flat::Camera>(
        cactus_gen::detail::generated_named_slots().drawing_rules.View);
    camera.offset.x += static_cast<float>(kCameraStep);
    run_frames(registry, 2);
    const Image moved = screenshot("drawing_rules_visual_moved.png");
    CloseWindow();

    REQUIRE(at_rest.data != nullptr);
    REQUIRE(moved.data != nullptr);

    // The fragment handler reads each row's heat: the hot dot is red, the cool one blue.
    CHECK(reddish(GetImageColor(at_rest, kHotX, kHotY)));
    CHECK(bluish(GetImageColor(at_rest, kCoolX, kCoolY)));
    // A rule in layers.sprites is under a ScreenLabel.
    CHECK(has_ink(at_rest, kHotCaption));
    // The link carries the sink's position to the shader.
    CHECK(cactus_visual_test::colors_close(
        GetImageColor(at_rest, kSinkX, kSinkY), Color{.r = 0, .g = 192, .b = 0, .a = 255}, 40));
    // The screen size reaches the shader, and a rule after every layer covers the label under it.
    CHECK(cactus_visual_test::colors_close(
        GetImageColor(at_rest, kBadgeX, kBadgeY), Color{.r = 255, .g = 192, .b = 0, .a = 255}, 40));
    CHECK_FALSE(has_ink(at_rest, kCornerCaption));
    // A stage calls std.noise.
    CHECK(shows_noise(at_rest));

    // The world moves with the camera; the screen-space badge stays.
    CHECK(bluish(GetImageColor(moved, kCoolX - kCameraStep, kCoolY)));
    CHECK_FALSE(bluish(GetImageColor(moved, kCoolX, kCoolY)));
    CHECK(cactus_visual_test::colors_close(
        GetImageColor(moved, kBadgeX, kBadgeY), Color{.r = 255, .g = 192, .b = 0, .a = 255}, 40));

    UnloadImage(at_rest);
    UnloadImage(moved);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
