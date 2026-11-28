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

namespace backend = cactus::runtime::entt_backend;

// Layout of examples/stdlib-fixtures/drawing_rule_batching.cactus, in window pixels.
constexpr int kSpacing         = 60;
constexpr int kOrigin          = 40;
constexpr int kTop             = 60;
constexpr int kBottom          = 140;
constexpr int kQuarter         = 10;
constexpr int kOnlyOne         = 15;  // from a pair's center into the part only one of its quads covers
constexpr unsigned char kUnder = 32;
constexpr int kTolerance       = 3;

int center_x(int index) {
    return kOrigin + (index * kSpacing);
}

bool near(Color pixel, int r, int g, int b) {
    UNSCOPED_INFO("pixel r=" << static_cast<int>(pixel.r) << " g=" << static_cast<int>(pixel.g)
                             << " b=" << static_cast<int>(pixel.b) << ", expected " << r << " " << g << " " << b);
    return std::abs(pixel.r - r) <= kTolerance && std::abs(pixel.g - g) <= kTolerance &&
           std::abs(pixel.b - b) <= kTolerance;
}

void run_frames(entt::registry& registry, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        BeginDrawing();
        ClearBackground(Color{.r = kUnder, .g = kUnder, .b = kUnder, .a = 255});
        backend::generated_inject_external_event(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        backend::generated_drain_external_events(registry);
        EndDrawing();
    }
}

struct Shot {
    Image image{};

    Shot() {
        const auto config = backend::generated_project_config();
        InitWindow(config.window_width, config.window_height, config.window_title);
        SetTargetFPS(config.target_fps);
        entt::registry registry;
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
        // Two frames: see test_particle_burst_visual.cpp for why one is not enough.
        run_frames(registry, 2);
        const std::string path = std::string(CACTUS_VISUAL_TEST_OUTPUT_DIR) + "/drawing_rule_batching_visual.png";
        TakeScreenshot(path.c_str());
        image = LoadImage(path.c_str());
        CloseWindow();
    }

    Shot(const Shot&)            = delete;
    Shot& operator=(const Shot&) = delete;
    Shot(Shot&&)                 = delete;
    Shot& operator=(Shot&&)      = delete;

    ~Shot() {
        UnloadImage(image);
    }

    [[nodiscard]] Color top(int index, int dx = 0, int dy = 0) const {
        return GetImageColor(image, center_x(index) + dx, kTop + dy);
    }

    [[nodiscard]] Color bottom(int index) const {
        return GetImageColor(image, center_x(index), kBottom);
    }
};

// One window for the whole run: textures loaded for one GL context are not valid in the next.
const Shot& the_shot() {
    static const Shot shot;
    return shot;
}

}  // namespace

TEST_CASE("drawing rule batching visual: a batch keeps the draw order and each row's values",
          "[example-behavior][drawing-rules][batching][visual]") {
    const Shot& shot = the_shot();
    REQUIRE(shot.image.data != nullptr);

    // Higher depth covers lower depth, though it was created first.
    CHECK(near(shot.top(0, -kOnlyOne), 255, 0, 0));
    CHECK(near(shot.top(0), 0, 255, 0));
    CHECK(near(shot.top(0, kOnlyOne), 0, 255, 0));
    // A mirrored quad still draws.
    CHECK(near(shot.top(1), 0, 0, 255));
    // An int row value reaches the stage exactly: 16777217 is not 16777216.
    CHECK(near(shot.top(2), 0, 255, 0));
    CHECK(near(shot.top(3), 255, 0, 0));
    // Two lights of one batch add where they overlap.
    CHECK(near(shot.top(4, -kQuarter), 96, 96, 96));
    CHECK(near(shot.top(4, kQuarter), 160, 160, 160));
}

TEST_CASE("drawing rule batching visual: each texture run shows its own texture",
          "[example-behavior][drawing-rules][batching][visual]") {
    const Shot& shot = the_shot();
    REQUIRE(shot.image.data != nullptr);

    for (const int index : {5, 6, 8}) {
        INFO("tile " << index);
        CHECK(near(shot.top(index, -kQuarter, -kQuarter), 255, 0, 0));
        CHECK(near(shot.top(index, kQuarter, kQuarter), 255, 255, 255));
    }
    CHECK(near(shot.top(7), 255, 255, 0));
}

TEST_CASE("drawing rule batching visual: only rows that pass where: are drawn",
          "[example-behavior][drawing-rules][batching][visual]") {
    const Shot& shot = the_shot();
    REQUIRE(shot.image.data != nullptr);

    for (const int index : {0, 2, 4}) {
        INFO("dot " << index);
        CHECK(near(shot.bottom(index), 255, 255, 255));
    }
    for (const int index : {1, 3, 5, 7}) {
        INFO("hidden " << index);
        CHECK(near(shot.bottom(index), kUnder, kUnder, kUnder));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
