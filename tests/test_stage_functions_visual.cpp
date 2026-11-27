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

#include <string>

namespace {

// Layout of examples/stdlib-fixtures/stage_functions.cactus, in window pixels.
constexpr int kCases   = 54;
constexpr int kColumns = 9;
constexpr int kOrigin  = 30;
constexpr int kSpacing = 40;

void run_frames(entt::registry& registry, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        BeginDrawing();
        ClearBackground(BLACK);
        cactus::runtime::entt_backend::generated_inject_external_event(
            cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
        EndDrawing();
    }
}

}  // namespace

TEST_CASE("stage functions visual: a shader computes every case as the CPU does",
          "[example-behavior][drawing-rules][stage-functions][visual]") {
    const auto config = cactus::runtime::entt_backend::generated_project_config();
    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    // `on load` spawns the probes.
    cactus::runtime::entt_backend::generated_run_boundary_activation(registry, cactus_gen::stdlib::core::loadEvent{});

    // Two frames: see test_particle_burst_visual.cpp for why one is not enough.
    run_frames(registry, 2);
    const std::string path = std::string(CACTUS_VISUAL_TEST_OUTPUT_DIR) + "/stage_functions_visual.png";
    TakeScreenshot(path.c_str());
    const Image shot = LoadImage(path.c_str());
    CloseWindow();
    REQUIRE(shot.data != nullptr);

    // Green: the shader agrees with the CPU. Red: an integer differs. Blue: a float differs.
    for (int index = 0; index < kCases; ++index) {
        const int x        = kOrigin + ((index % kColumns) * kSpacing);
        const int y        = kOrigin + ((index / kColumns) * kSpacing);
        const Color pixel  = GetImageColor(shot, x, y);
        UNSCOPED_INFO("case " << index << ": r=" << static_cast<int>(pixel.r) << " g=" << static_cast<int>(pixel.g)
                              << " b=" << static_cast<int>(pixel.b));
        CHECK((pixel.r == 0 && pixel.g == 255 && pixel.b == 0));
    }
    UnloadImage(shot);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
