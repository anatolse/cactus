// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace {

namespace cam = cactus_gen::stdlib::camera::volume;
using cactus_gen::camera_view::Mover;
using cactus_gen::camera_view::Recorder;
using cactus_gen::stdlib::camera::viewport::ScreenSize;
using cactus_gen::stdlib::camera::viewport::Viewport;
using cactus_gen::stdlib::transform::volume::WorldTransform;
using cactus_headless_test::drive_frame;

auto& slots() {
    return cactus_gen::detail::generated_named_slots();
}

struct World {
    entt::registry registry;

    explicit World(int width = 1280, int height = 720, bool window_ready = false) {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_screen_size(width, height);
        cactus_raylib_fake::set_window_ready(window_ready);
        cactus_headless_test::init_and_load(registry);
    }

    const cam::CameraView& view(entt::entity camera) {
        REQUIRE(registry.all_of<cam::CameraView>(camera));
        return registry.get<cam::CameraView>(camera);
    }

    Recorder& recorder() { return registry.get<Recorder>(slots().camera_view.Log); }
};

float distance(Vector2 a, Vector2 b) {
    return Vector2Distance(a, b);
}

}  // namespace

TEST_CASE("Screen holds the configured window size", "[runtime][camera-view]") {
    World world;
    drive_frame(world.registry);
    const auto& screen = world.registry.get<ScreenSize>(slots().stdlib.camera.viewport.Screen);
    CHECK(cactus_raylib_fake::approx_equal(screen.size, Vector2{.x = 1280.0F, .y = 720.0F}));
    CHECK(cactus_raylib_fake::approx_equal(world.recorder().input_screen, Vector2{.x = 1280.0F, .y = 720.0F}));
}

TEST_CASE("A resize is seen in the next frame's input phase", "[runtime][camera-view]") {
    World world;
    drive_frame(world.registry);
    cactus_raylib_fake::set_screen_size(1024, 768);
    drive_frame(world.registry);
    CHECK(cactus_raylib_fake::approx_equal(world.recorder().input_screen, Vector2{.x = 1024.0F, .y = 768.0F}));
}

TEST_CASE("A view follows its moving camera", "[runtime][camera-view]") {
    World world;
    const auto right = slots().camera_view.Right;
    world.registry.get<Mover>(right).step = Vector3{.x = 1.0F, .y = 0.0F, .z = -0.5F};
    for (int frame = 0; frame < 2; ++frame) {
        drive_frame(world.registry);
        const auto& pose = world.registry.get<WorldTransform>(right);
        const auto& view = world.view(right);
        const auto basis = cactus::runtime::entt_backend::camera_basis(pose.rotation);
        CHECK(cactus_raylib_fake::approx_equal(view.position, pose.position));
        CHECK(cactus_raylib_fake::approx_equal(view.forward, basis.forward));
        CHECK(cactus_raylib_fake::approx_equal(view.right, basis.right));
        CHECK(cactus_raylib_fake::approx_equal(view.up, basis.up));
        CHECK(view.fov_y == 45.0F);
    }
    CHECK(world.view(right).position.x == 8.0F);
}

TEST_CASE("A split viewport's view has its pixel rectangle", "[runtime][camera-view]") {
    World world;
    drive_frame(world.registry);
    const auto& right = world.view(slots().camera_view.Right);
    CHECK(cactus_raylib_fake::approx_equal(right.rect_min, Vector2{.x = 640.0F, .y = 0.0F}));
    CHECK(cactus_raylib_fake::approx_equal(right.rect_size, Vector2{.x = 640.0F, .y = 720.0F}));
    const auto& left = world.view(slots().camera_view.Left);
    CHECK(cactus_raylib_fake::approx_equal(left.rect_min, Vector2{.x = 0.0F, .y = 0.0F}));
    CHECK(cactus_raylib_fake::approx_equal(left.rect_size, Vector2{.x = 640.0F, .y = 720.0F}));
}

TEST_CASE("Only cameras with a viewport have a view", "[runtime][camera-view]") {
    World world;
    drive_frame(world.registry);
    CHECK_FALSE(world.registry.all_of<cam::CameraView>(slots().camera_view.Blind));

    world.registry.remove<Viewport>(slots().camera_view.Right);
    drive_frame(world.registry);
    CHECK_FALSE(world.registry.all_of<cam::CameraView>(slots().camera_view.Right));
    CHECK(world.registry.all_of<cam::CameraView>(slots().camera_view.Left));
}

TEST_CASE("A ray through a pixel projects back to that pixel", "[runtime][camera-view]") {
    World world;
    drive_frame(world.registry);
    for (const auto camera : {slots().camera_view.Left, slots().camera_view.Right}) {
        const auto& view = world.view(camera);
        for (float u = 0.0F; u <= 1.0F; u += 0.125F) {
            for (float v = 0.0F; v <= 1.0F; v += 0.125F) {
                const Vector2 pixel{.x = view.rect_min.x + (u * view.rect_size.x),
                                    .y = view.rect_min.y + (v * view.rect_size.y)};
                const auto ray = cam::ray(view, pixel);
                CHECK(Vector3Length(ray.direction) == Catch::Approx(1.0F));
                CHECK(Vector3DotProduct(Vector3Subtract(ray.origin, view.position), view.forward) ==
                      Catch::Approx(view.near));
                for (const float d : {0.01F, 1.0F, 10.0F, 100.0F, 900.0F}) {
                    const auto projected = cam::to_screen(view, Vector3Add(ray.origin, Vector3Scale(ray.direction, d)));
                    CHECK(projected.in_front);
                    CHECK(distance(projected.position, pixel) < 0.01F);
                }
            }
        }
    }
}

TEST_CASE("A point behind the camera or on its near plane is not in front", "[runtime][camera-view]") {
    World world;
    drive_frame(world.registry);
    const auto& view = world.view(slots().camera_view.Right);
    for (const float d : {-5.0F, 0.0F, view.near * 0.5F, view.near * 0.99F}) {
        const auto projected = cam::to_screen(view, Vector3Add(view.position, Vector3Scale(view.forward, d)));
        CHECK_FALSE(projected.in_front);
        CHECK(cactus_raylib_fake::approx_equal(projected.position, view.rect_min));
    }
}

TEST_CASE("A view with an empty rectangle projects nothing", "[runtime][camera-view]") {
    World world;
    drive_frame(world.registry);
    auto view        = world.view(slots().camera_view.Right);
    view.rect_size.x = 0.0F;
    CHECK_FALSE(cam::to_screen(view, Vector3Add(view.position, Vector3Scale(view.forward, 10.0F))).in_front);
    const auto ray = cam::ray(view, Vector2{.x = 700.0F, .y = 100.0F});
    CHECK(cactus_raylib_fake::approx_equal(ray.origin, view.position));
    CHECK(cactus_raylib_fake::approx_equal(ray.direction, view.forward));
}

TEST_CASE("A ray meets a plane in front of it and misses parallel and behind planes", "[runtime][camera-view]") {
    const cam::ScreenRay down{.origin    = Vector3{.x = 1.0F, .y = 10.0F, .z = 2.0F},
                              .direction = Vector3{.x = 0.0F, .y = -1.0F, .z = 0.0F}};
    constexpr Vector3 kGround{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    constexpr Vector3 kUp{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const auto hit = cam::ray_plane(down, kGround, kUp);
    CHECK(hit.hit);
    CHECK(hit.distance == Catch::Approx(10.0F));
    CHECK(cactus_raylib_fake::approx_equal(hit.point, Vector3{.x = 1.0F, .y = 0.0F, .z = 2.0F}));
    CHECK(cactus_raylib_fake::approx_equal(hit.normal, kUp));
    CHECK(cactus_raylib_fake::approx_equal(cam::ray_plane(down, kGround, Vector3Negate(kUp)).normal, kUp));

    const cam::ScreenRay level{.origin = down.origin, .direction = Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}};
    CHECK_FALSE(cam::ray_plane(level, kGround, kUp).hit);
    const cam::ScreenRay away{.origin = down.origin, .direction = kUp};
    CHECK_FALSE(cam::ray_plane(away, kGround, kUp).hit);
}

TEST_CASE("A rule finds the ground under a pixel", "[runtime][camera-view]") {
    World world;
    drive_frame(world.registry);
    drive_frame(world.registry);
    const auto& recorder = world.recorder();
    REQUIRE(recorder.ground_hit);
    CHECK(recorder.ground.y == Catch::Approx(0.0F).margin(1e-4));
    const auto& view = world.view(slots().camera_view.Right);
    const auto back  = cam::to_screen(view, recorder.ground);
    CHECK(back.in_front);
    CHECK(distance(back.position, Vector2{.x = 300.0F, .y = 400.0F}) < 0.01F);
}

TEST_CASE("Each 3D viewport renders into its own rectangle", "[runtime][camera-view]") {
    World world(1280, 720, true);
    drive_frame(world.registry);
    const auto& log = cactus_raylib_fake::call_log();
    std::vector<cactus_raylib_fake::RecordedViewport> viewports;
    for (const auto& call : log) {
        if (const auto* recorded = std::get_if<cactus_raylib_fake::RecordedViewport>(&call)) {
            viewports.push_back(*recorded);
        }
    }
    const auto has = [&](int x, int y, int width, int height) {
        return std::ranges::any_of(viewports, [&](const auto& v) {
            return v.x == x && v.y == y && v.width == width && v.height == height;
        });
    };
    CHECK(has(0, 0, 640, 720));
    CHECK(has(640, 0, 640, 720));
    CHECK(has(0, 0, 1280, 720));
}

TEST_CASE("to_screen agrees with the renderer's projection", "[runtime][camera-view]") {
    World world(1280, 720, true);
    drive_frame(world.registry);
    const auto right       = slots().camera_view.Right;
    const Vector3 position = world.registry.get<WorldTransform>(right).position;
    drive_frame(world.registry);

    // The last frame's 3D block for the right camera: its camera, GL viewport and projection.
    const auto& log = cactus_raylib_fake::call_log();
    std::optional<Camera3D> camera;
    std::optional<cactus_raylib_fake::RecordedViewport> viewport;
    std::optional<Matrix> projection;
    for (const auto& call : log) {
        if (const auto* begin = std::get_if<cactus_raylib_fake::RecordedBeginMode3D>(&call)) {
            camera.reset();
            viewport.reset();
            projection.reset();
            if (cactus_raylib_fake::approx_equal(begin->camera.position, position)) {
                camera = begin->camera;
            }
        } else if (const auto* recorded = std::get_if<cactus_raylib_fake::RecordedViewport>(&call)) {
            if (camera.has_value() && !viewport.has_value()) {
                viewport = *recorded;
            }
        } else if (const auto* matrix = std::get_if<cactus_raylib_fake::RecordedSetMatrixProjection>(&call)) {
            if (camera.has_value() && !projection.has_value()) {
                projection = matrix->projection;
            }
        }
        if (camera.has_value() && viewport.has_value() && projection.has_value()) {
            break;
        }
    }
    REQUIRE(camera.has_value());
    REQUIRE(viewport.has_value());
    REQUIRE(projection.has_value());

    const Vector3 marker = world.registry.get<WorldTransform>(slots().camera_view.Block).position;
    Quaternion clip{.x = marker.x, .y = marker.y, .z = marker.z, .w = 1.0F};
    clip = QuaternionTransform(clip, MatrixLookAt(camera->position, camera->target, camera->up));
    clip = QuaternionTransform(clip, *projection);
    REQUIRE(clip.w > 0.0F);
    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    const Vector2 rendered{
        .x = static_cast<float>(viewport->x) + ((ndc_x + 1.0F) * 0.5F * static_cast<float>(viewport->width)),
        .y = 720.0F - (static_cast<float>(viewport->y) + ((ndc_y + 1.0F) * 0.5F * static_cast<float>(viewport->height))),
    };
    const auto& recorder = world.recorder();
    CHECK(recorder.marker_in_front);
    CHECK(distance(recorder.marker_screen, rendered) <= 0.5F);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
