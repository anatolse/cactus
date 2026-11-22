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

#include <catch2/catch_test_macros.hpp>
#include <raylib.h>

#include <cstdlib>
#include <string>

namespace {

using Picture = cactus_gen::drawn_picture::Picture;

void draw_frame(entt::registry& registry) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    cactus::runtime::entt_backend::generated_inject_external_event(
        cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
    EndDrawing();
}

// Draw twice: TakeScreenshot reads the buffer drawn most recently.
Image capture(entt::registry& registry, const std::string& path) {
    draw_frame(registry);
    draw_frame(registry);
    TakeScreenshot(path.c_str());
    return LoadImage(path.c_str());
}

// The flat camera centers world (0, 0) on the screen.
Color pixel_at_world(const Image& image, const float x, const float y) {
    return GetImageColor(image,
                         static_cast<int>(x) + (GetScreenWidth() / 2),
                         static_cast<int>(y) + (GetScreenHeight() / 2));
}

bool near_color(const Color actual, const Color expected) {
    constexpr int kTolerance = 8;
    const auto close         = [](unsigned char a, unsigned char b) { return std::abs(int{a} - int{b}) <= kTolerance; };
    return close(actual.r, expected.r) && close(actual.g, expected.g) && close(actual.b, expected.b);
}

}  // namespace

TEST_CASE("drawn picture visual: a shape is drawn where and how its picture says, then as authored again",
          "[example-behavior][drawn-picture][visual]") {
    const auto config = cactus::runtime::entt_backend::generated_project_config();
    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const std::string output_dir = CACTUS_VISUAL_TEST_OUTPUT_DIR;
    const Image drawn            = capture(registry, output_dir + "/drawn_picture_on.png");
    for (auto&& [entity, picture] : registry.view<Picture>().each()) {
        picture.shown = false;
    }
    const Image truth = capture(registry, output_dir + "/drawn_picture_off.png");
    CloseWindow();
    REQUIRE(drawn.data != nullptr);
    REQUIRE(truth.data != nullptr);

    // The box turns about its drawn corner (104, 100); (106, 106.8) is its middle, (101, 101) is left of it.
    // Drawn color #C0808080 over the black viewport.
    const Color black{.r = 0, .g = 0, .b = 0, .a = 255};
    CHECK(near_color(pixel_at_world(drawn, 106.0F, 106.8F), Color{.r = 96, .g = 64, .b = 64, .a = 255}));
    CHECK(near_color(pixel_at_world(drawn, 101.0F, 101.0F), black));
    CHECK(near_color(pixel_at_world(truth, 101.0F, 101.0F), Color{.r = 128, .g = 128, .b = 128, .a = 255}));

    // A glow above white saturates once at output.
    CHECK(near_color(pixel_at_world(drawn, -195.0F, 105.0F), Color{.r = 255, .g = 255, .b = 255, .a = 255}));

    UnloadImage(drawn);
    UnloadImage(truth);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
