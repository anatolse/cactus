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

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace {

namespace backend = cactus::runtime::entt_backend;
namespace fake    = cactus_raylib_fake;

using Picture        = cactus_gen::drawn_picture_volume::Picture;
using WorldTransform = cactus_gen::stdlib::transform::volume::WorldTransform;
using MeshRenderer   = cactus_gen::stdlib::render::meshes::Renderer;
using CameraView     = cactus_gen::stdlib::camera::volume::CameraView;
using TextLabel      = cactus_gen::stdlib::render::text::TextLabel;

auto& slots() {
    return cactus_gen::detail::generated_named_slots().drawn_picture_volume;
}

bool near_color(const Color actual, const Color expected) {
    const auto close = [](unsigned char a, unsigned char b) { return std::abs(int{a} - int{b}) <= 1; };
    return close(actual.r, expected.r) && close(actual.g, expected.g) && close(actual.b, expected.b) &&
           close(actual.a, expected.a);
}

bool near(const float actual, const float expected) {
    return std::abs(actual - expected) < 0.001F;
}

const fake::RecordedDrawMesh* mesh_at(const Vector3 position) {
    return fake::find_call<fake::RecordedDrawMesh>(fake::call_log(), [&](const auto& call) {
        return fake::approx_equal(call.transform, position);
    });
}

struct World {
    entt::registry registry;

    explicit World(bool window_ready = true) {
        fake::reset();
        fake::set_window_ready(window_ready);
        cactus_headless_test::init_and_load(registry);
        // The fake GL cannot bake 3D text, so the label shows only without a window.
        registry.get<TextLabel>(slots().Label).visible = !window_ready;
        frame();
    }

    void frame() {
        fake::clear_call_log();
        backend::reset_render_debug_state();
        cactus_headless_test::drive_frame(registry);
    }

    void picture_off() {
        for (const auto [entity, picture] : registry.view<Picture>().each()) {
            picture.shown = false;
        }
    }
};

}  // namespace

TEST_CASE("drawn picture volume: a mesh is drawn at its drawn pose and color",
          "[example-behavior][drawn-picture]") {
    World world;
    const auto* block = mesh_at(Vector3{.x = -2.0F, .y = 1.0F, .z = 0.0F});
    REQUIRE(block != nullptr);
    CHECK(near_color(block->diffuse_color, Color{.r = 192, .g = 128, .b = 128, .a = 128}));
    CHECK(fake::approx_equal(world.registry.get<WorldTransform>(slots().Block).position,
                             Vector3{.x = -2.0F, .y = 0.0F, .z = 0.0F}));
    CHECK(fake::colors_equal(to_raylib_color(world.registry.get<MeshRenderer>(slots().Block).color),
                             Color{.r = 128, .g = 128, .b = 128, .a = 255}));
}

TEST_CASE("drawn picture volume: a model is drawn at its drawn pose and color",
          "[example-behavior][drawn-picture]") {
    World world;
    const auto* model = mesh_at(Vector3{.x = 2.0F, .y = 0.0F, .z = -1.0F});
    REQUIRE(model != nullptr);
    CHECK(fake::colors_equal(model->diffuse_color, Color{.r = 0, .g = 255, .b = 0, .a = 255}));
}

TEST_CASE("drawn picture volume: a 3D label is submitted at its drawn pose and color",
          "[example-behavior][drawn-picture]") {
    World world(false);
    const auto& debug = backend::render_debug_state();
    REQUIRE(debug.submitted_text_3d_positions.size() == 1);
    CHECK(fake::approx_equal(debug.submitted_text_3d_positions.front(), Vector3{.x = 1.0F, .y = 2.0F, .z = 0.0F}));
    REQUIRE(debug.submitted_text_3d_colors.size() == 1);
    CHECK(fake::colors_equal(to_raylib_color(debug.submitted_text_3d_colors.front()),
                             Color{.r = 0, .g = 0, .b = 255, .a = 255}));
}

TEST_CASE("drawn picture volume: lights take the drawn pose and the raw drawn color",
          "[example-behavior][drawn-picture]") {
    World world;
    const auto& debug = backend::render_debug_state();
    REQUIRE(debug.uploaded_light_colors.size() == 2);
    REQUIRE(debug.uploaded_light_positions.size() == 2);
    CHECK(fake::approx_equal(debug.uploaded_light_positions.front(), Vector3{.x = 0.0F, .y = 3.5F, .z = 0.0F}));
    const auto torch = debug.uploaded_light_colors.front();
    CHECK(near(torch.r, 1.0F + (128.0F / 255.0F)));
    CHECK(near(torch.g, 1.0F));
    const auto sun = debug.uploaded_light_colors.back();
    CHECK(near(sun.r, 128.0F / 255.0F));
    CHECK(near(sun.g, 0.0F));
    CHECK(near(sun.b, 0.0F));
}

TEST_CASE("drawn picture volume: the viewport draws from the camera's drawn pose, its view keeps the true pose",
          "[example-behavior][drawn-picture]") {
    World world;
    const auto* begin = fake::find_call<fake::RecordedBeginMode3D>(fake::call_log());
    REQUIRE(begin != nullptr);
    CHECK(fake::approx_equal(begin->camera.position, Vector3{.x = 0.1F, .y = 1.0F, .z = 6.0F}));
    world.frame();
    REQUIRE(world.registry.all_of<CameraView>(slots().Eye));
    CHECK(fake::approx_equal(world.registry.get<CameraView>(slots().Eye).position,
                             Vector3{.x = 0.0F, .y = 1.0F, .z = 6.0F}));
}

TEST_CASE("drawn picture volume: with no projection the truth is drawn again", "[example-behavior][drawn-picture]") {
    World world;
    world.picture_off();
    world.frame();
    const auto* block = mesh_at(Vector3{.x = -2.0F, .y = 0.0F, .z = 0.0F});
    REQUIRE(block != nullptr);
    CHECK(fake::colors_equal(block->diffuse_color, Color{.r = 128, .g = 128, .b = 128, .a = 255}));
    const auto* begin = fake::find_call<fake::RecordedBeginMode3D>(fake::call_log());
    REQUIRE(begin != nullptr);
    CHECK(fake::approx_equal(begin->camera.position, Vector3{.x = 0.0F, .y = 1.0F, .z = 6.0F}));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
