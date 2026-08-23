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

// dsl-render-passes "Instances drawn by a vertex-stage handler render
// independently": CACTUS_RAYLIB_FAKE makes ensure_render_pass_shader()
// return nullptr unconditionally (runtime.cpp), so no headless behavioral
// test can ever reach the real draw path this requirement is about. This
// test links the real raylib backend and drives an actual GL context
// instead — see cactus_add_render_pass_visual_test in CMakeLists.txt.
//
// It doesn't go through a real mouse click: raylib's public API has no way
// to inject a synthetic button press for a real (non-CACTUS_RAYLIB_FAKE)
// build — IsMouseButtonPressed is a direct passthrough to actual OS input
// (raylib_io.hpp) — and platform-specific OS input injection would violate
// this test's own portability requirement. Instead it reproduces a click's
// *effect* directly: spawning particle entities at distinct positions
// through the same generated entity-creation function EmitParticleBurst
// itself calls, then driving one real frame so the render pass actually
// draws them.
TEST_CASE("particle-burst visual: instances render at their own positions, not one shared position",
          "[example-behavior][particle-burst][render-passes][visual]") {
    const auto config = cactus::runtime::entt_backend::generated_project_config();
    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    // Spread well beyond one particle's diameter (PARTICLE_RADIUS * 2 = 10px)
    // so a collapsed-to-one-shared-position regression is unambiguous.
    constexpr int kInstanceCount = 8;
    constexpr float kSpacingPx   = 40.0F;
    constexpr float kStartX      = 100.0F;
    constexpr float kY           = 300.0F;
    for (int i = 0; i < kInstanceCount; ++i) {
        const auto entity = create_particle_burst__particle_template(registry);
        registry.get<std_transform_flat__WorldTransform>(entity).position =
            Vector2{.x = kStartX + (static_cast<float>(i) * kSpacingPx), .y = kY};
    }

    // glReadPixels (which TakeScreenshot uses) reads the GL back buffer.
    // With double buffering, the buffer just drawn into becomes the FRONT
    // buffer the instant EndDrawing()'s SwapBuffers runs, so a screenshot
    // taken after only one drawn frame reads the *other*, not-yet-drawn-into
    // buffer (garbage/black on a freshly created window). Drawing the same
    // (idempotent) content twice means that by the second EndDrawing, the
    // current back buffer holds what the first frame actually rendered.
    constexpr int kFramesBeforeScreenshot = 2;
    for (int frame = 0; frame < kFramesBeforeScreenshot; ++frame) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
        EndDrawing();
    }

    const std::string screenshot_path = std::string(CACTUS_VISUAL_TEST_OUTPUT_DIR) + "/particle_burst_visual_test.png";
    TakeScreenshot(screenshot_path.c_str());
    CloseWindow();

    const Image screenshot = LoadImage(screenshot_path.c_str());
    REQUIRE(screenshot.data != nullptr);
    const auto bbox = cactus_visual_test::non_background_bounding_box(screenshot, RAYWHITE);
    UnloadImage(screenshot);

    REQUIRE(bbox.has_value());
    // A single particle (diameter 10px) alone could never reach this span;
    // only multiple independently-positioned instances can, across the
    // (kInstanceCount - 1) * kSpacingPx = 280px layout above.
    constexpr int kMinSpanPixels = 100;
    CHECK(bbox->width() > kMinSpanPixels);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
