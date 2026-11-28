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

// Layout of examples/stdlib-fixtures/cover_and_light.cactus, in window pixels.
constexpr int kSpacing    = 60;
constexpr int kOrigin     = 40;
constexpr int kRow        = 60;
constexpr int kQuarter    = 10;  // from a square's center to the center of one of its quarters
constexpr unsigned char kUnder = 32;
constexpr int kTolerance  = 3;

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
        const std::string path = std::string(CACTUS_VISUAL_TEST_OUTPUT_DIR) + "/cover_and_light_visual.png";
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

    [[nodiscard]] Color at(int index, int dx = 0, int dy = 0) const {
        return GetImageColor(image, center_x(index) + dx, kRow + dy);
    }
};

// One window for the whole run: textures loaded for one GL context are not valid in the next.
const Shot& the_shot() {
    static const Shot shot;
    return shot;
}

}  // namespace

TEST_CASE("cover and light visual: each output reaches the pixel as the composition says",
          "[example-behavior][drawing-rules][visual]") {
    const Shot& shot = the_shot();
    REQUIRE(shot.image.data != nullptr);

    // A cover hides what is under it.
    CHECK(near(shot.at(0), 255, 0, 0));
    // A light adds to what is under it.
    CHECK(near(shot.at(1), 96, 96, 96));
    // Two lights add twice where they overlap.
    CHECK(near(shot.at(2, -kQuarter), 96, 96, 96));
    CHECK(near(shot.at(2, kQuarter), 160, 160, 160));
    // Half a black cover over white, with red light added.
    CHECK(near(shot.at(3), 191, 127, 127));
    // A cover with alpha 0 draws nothing, whatever its color.
    CHECK(near(shot.at(4), kUnder, kUnder, kUnder));
    // A light brighter than 1: its alpha counts as 1, each channel clamps on the write.
    CHECK(near(shot.at(5), 255, 255, kUnder));
    // A cover alpha above 1 counts as 1, so the cover's own color shows over white.
    CHECK(near(shot.at(6), 128, 128, 128));
    // Light only adds: a negative channel subtracts nothing.
    CHECK(near(shot.at(12), kUnder, kUnder, kUnder));
}

TEST_CASE("cover and light visual: a fragment handler samples textures", "[example-behavior][drawing-rules][visual]") {
    const Shot& shot = the_shot();
    REQUIRE(shot.image.data != nullptr);

    // The image's top-left is the quad's top-left.
    CHECK(near(shot.at(7, -kQuarter, -kQuarter), 255, 0, 0));
    CHECK(near(shot.at(7, kQuarter, -kQuarter), 0, 255, 0));
    CHECK(near(shot.at(7, -kQuarter, kQuarter), 0, 0, 255));
    CHECK(near(shot.at(7, kQuarter, kQuarter), 255, 255, 255));
    // A UV right of the image reads its right edge.
    CHECK(near(shot.at(8, -kQuarter, kQuarter), 0, 255, 0));
    // Rows with different textures each show their own.
    CHECK(near(shot.at(9, -kQuarter, -kQuarter), 255, 0, 0));
    CHECK(near(shot.at(9, kQuarter, kQuarter), 255, 255, 255));
    CHECK(near(shot.at(10), 255, 255, 0));
    // A missing texture draws nothing and is noted once.
    CHECK(near(shot.at(11), kUnder, kUnder, kUnder));
    CHECK(backend::render_debug_state().missing_assets == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
