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
#include <string>

namespace {

namespace game   = cactus_gen::drawn_picture_volume;
namespace meshes = cactus_gen::stdlib::render::meshes;
namespace models = cactus_gen::stdlib::render::models;
namespace text   = cactus_gen::stdlib::render::text;

auto& slots() {
    return cactus_gen::detail::generated_named_slots().drawn_picture_volume;
}

void draw_frame(entt::registry& registry) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    cactus::runtime::entt_backend::generated_inject_external_event(
        cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
    EndDrawing();
}

// What a screenshot shows against the black viewport: where the lit pixels sit and how bright they get.
struct Seen {
    float center_x = 0.0F;
    long red       = 0;
    long green     = 0;
    int peak       = 0;
};

// Draw twice: TakeScreenshot reads the buffer drawn most recently.
Seen capture(entt::registry& registry, const std::string& path) {
    draw_frame(registry);
    draw_frame(registry);
    TakeScreenshot(path.c_str());
    const Image image = LoadImage(path.c_str());
    REQUIRE(image.data != nullptr);
    Seen seen;
    long lit   = 0;
    double sum = 0.0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const Color pixel    = GetImageColor(image, x, y);
            const int brightness = int{pixel.r} + int{pixel.g} + int{pixel.b};
            seen.peak            = std::max(seen.peak, brightness);
            if (brightness < 30) {
                continue;
            }
            ++lit;
            sum += x;
            seen.red += pixel.r;
            seen.green += pixel.g;
        }
    }
    UnloadImage(image);
    REQUIRE(lit > 0);
    seen.center_x = static_cast<float>(sum / static_cast<double>(lit));
    return seen;
}

void show(entt::registry& registry, const entt::entity entity, const bool shown) {
    registry.get<game::Picture>(entity).shown = shown;
}

}  // namespace

TEST_CASE("drawn picture volume visual: a model is drawn at its drawn pose and color; mesh alpha is recorded",
          "[example-behavior][drawn-picture][visual]") {
    const auto config = cactus::runtime::entt_backend::generated_project_config();
    InitWindow(config.window_width, config.window_height, config.window_title);
    SetTargetFPS(config.target_fps);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    for (const auto entity : {slots().Eye, slots().Torch, slots().Sun}) {
        show(registry, entity, false);
    }
    registry.get<text::TextLabel>(slots().Label).visible = false;

    const std::string output_dir = CACTUS_VISUAL_TEST_OUTPUT_DIR;
    registry.get<meshes::Renderer>(slots().Block).visible = false;
    registry.get<game::Picture>(slots().Model).shift      = Vector3{.x = 1.5F, .y = 0.0F, .z = 0.0F};
    const Seen drawn_model = capture(registry, output_dir + "/drawn_model_on.png");
    show(registry, slots().Model, false);
    const Seen true_model = capture(registry, output_dir + "/drawn_model_off.png");

    registry.get<meshes::Renderer>(slots().Block).visible   = true;
    registry.get<models::ModelRenderer>(slots().Model).visible = false;
    auto& block_picture                                     = registry.get<game::Picture>(slots().Block);
    block_picture.shift                                     = Vector3{};
    block_picture.glow                                      = color_from_bytes(0, 0, 0, 0);
    block_picture.tint                                      = color_from_bytes(255, 255, 255, 255);
    const Seen opaque_block = capture(registry, output_dir + "/drawn_block_opaque.png");
    block_picture.tint      = color_from_bytes(255, 255, 255, 128);
    const Seen faded_block  = capture(registry, output_dir + "/drawn_block_faded.png");
    CloseWindow();

    CHECK(drawn_model.center_x > true_model.center_x + 20.0F);
    CHECK(drawn_model.green > 2 * drawn_model.red);
    CHECK(true_model.red > 0);
    WARN("mesh peak brightness: opaque " << opaque_block.peak << ", tint alpha 0.5 " << faded_block.peak);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
