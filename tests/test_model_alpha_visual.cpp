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

#include <algorithm>
#include <cmath>
#include <string>

namespace {

// stdlib-render-models: a per-instance ModelRenderer.color alpha must
// actually reduce visibility against the scene behind it. The shared lit
// fragment shader (runtime.cpp) previously computed its alpha channel from
// vec4 math that always exceeded 1.0 before the final gamma pow(), which GL
// then clamps to fully opaque -- CACTUS_RAYLIB_FAKE never reaches this
// shader, so only a real GL context can catch this. See
// cactus_add_render_pass_visual_test in CMakeLists.txt.
void draw_frame(entt::registry& registry) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
    EndDrawing();
}

// Same double-draw rationale as test_particle_burst_visual.cpp: the buffer
// TakeScreenshot reads is whichever one was drawn into most recently, so
// draw the same (idempotent) state twice before capturing.
int capture_peak_brightness(entt::registry& registry, const char* path) {
    draw_frame(registry);
    draw_frame(registry);
    TakeScreenshot(path);

    const Image screenshot = LoadImage(path);
    REQUIRE(screenshot.data != nullptr);
    int peak = 0;
    for (int y = 0; y < screenshot.height; ++y) {
        for (int x = 0; x < screenshot.width; ++x) {
            const Color pixel    = GetImageColor(screenshot, x, y);
            const int brightness = static_cast<int>(pixel.r) + static_cast<int>(pixel.g) + static_cast<int>(pixel.b);
            peak                 = std::max(peak, brightness);
        }
    }
    UnloadImage(screenshot);
    return peak;
}

bool near_x(const float value, const float target) {
    return std::abs(value - target) < 0.01F;
}

}  // namespace

TEST_CASE("model alpha visual: lower ModelRenderer.color alpha renders dimmer against a black background",
          "[example-behavior][model-tint][render-passes][visual]") {
    const auto config = cactus::runtime::entt_backend::generated_project_config();
    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    // StaticDefault (x=-2, alpha=255), StaticTinted (x=0, alpha=128), and
    // AnimatedTinted (x=2, alpha=64) -- isolate one at a time by hiding the
    // other two so overlapping silhouettes can't muddy the comparison.
    entt::entity default_entity  = entt::null;
    entt::entity static_entity   = entt::null;
    entt::entity animated_entity = entt::null;
    registry.view<std_transform_volume__WorldTransform, std_render_models__ModelRenderer>().each(
        [&](entt::entity entity,
            const std_transform_volume__WorldTransform& transform,
            std_render_models__ModelRenderer&) {
            if (near_x(transform.position.x, -2.0F)) {
                default_entity = entity;
            } else if (near_x(transform.position.x, 0.0F)) {
                static_entity = entity;
            } else if (near_x(transform.position.x, 2.0F)) {
                animated_entity = entity;
            }
        });
    REQUIRE(registry.valid(default_entity));
    REQUIRE(registry.valid(static_entity));
    REQUIRE(registry.valid(animated_entity));

    const auto isolate = [&](entt::entity visible_entity) {
        for (const auto entity : {default_entity, static_entity, animated_entity}) {
            registry.get<std_render_models__ModelRenderer>(entity).visible = (entity == visible_entity);
        }
    };

    const std::string output_dir = CACTUS_VISUAL_TEST_OUTPUT_DIR;
    isolate(default_entity);
    const int default_peak = capture_peak_brightness(registry, (output_dir + "/model_alpha_default.png").c_str());
    isolate(static_entity);
    const int static_peak = capture_peak_brightness(registry, (output_dir + "/model_alpha_static.png").c_str());
    isolate(animated_entity);
    const int animated_peak = capture_peak_brightness(registry, (output_dir + "/model_alpha_animated.png").c_str());
    CloseWindow();

    CHECK(default_peak > static_peak);
    CHECK(static_peak > animated_peak);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
