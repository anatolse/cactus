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

#include <optional>
#include <vector>

namespace {

namespace cam  = cactus_gen::stdlib::camera::volume;
namespace fake = cactus_raylib_fake;
using cactus_gen::minimap::CameraTarget;
using cactus_gen::minimap::Click;
using cactus_gen::stdlib::transform::volume::WorldTransform;
using cactus_headless_test::drive_frame;

constexpr float kScreenWidth  = 1280.0F;
constexpr float kScreenHeight = 720.0F;
constexpr Vector3 kMarkerOffset{.x = 0.0F, .y = 2.0F, .z = 0.0F};

auto& slots() {
    return cactus_gen::detail::generated_named_slots().minimap;
}

struct World {
    entt::registry registry;

    explicit World(bool window_ready = false) {
        fake::reset();
        fake::set_screen_size(static_cast<int>(kScreenWidth), static_cast<int>(kScreenHeight));
        fake::set_window_ready(window_ready);
        cactus_headless_test::init_and_load(registry);
        drive_frame(registry);
    }

    const cam::CameraView& view(entt::entity camera) {
        REQUIRE(registry.all_of<cam::CameraView>(camera));
        return registry.get<cam::CameraView>(camera);
    }

    [[nodiscard]] Vector3 position(entt::entity entity) const {
        return registry.get<WorldTransform>(entity).position;
    }
};

// One frame's mesh draws in the main view and in the minimap (the viewport at x = 960).
struct ViewDraws {
    std::vector<Matrix> main    = fake::mesh_draws_in_scissor(0);
    std::vector<Matrix> minimap = fake::mesh_draws_in_scissor(960);
};

bool drawn(const std::vector<Matrix>& draws, Vector3 position) {
    return fake::has_draw_at(draws, position, 0.001F);
}

}  // namespace

TEST_CASE("minimap: a camera is perspective unless it says otherwise", "[runtime][minimap][camera-view]") {
    World world;
    const auto& main = world.registry.get<cam::Camera>(slots().Main);
    CHECK(main.projection == cam::Projection::Perspective);
    CHECK(main.ortho_height == 10.0F);
    CHECK(world.view(slots().Main).projection == cam::Projection::Perspective);
    const auto& minimap = world.view(slots().Minimap);
    CHECK(minimap.projection == cam::Projection::Orthographic);
    CHECK(minimap.ortho_height == 30.0F);
}

TEST_CASE("minimap: the main view draws units and the minimap draws markers", "[runtime][minimap][visibility]") {
    World world(true);
    fake::clear_call_log();
    drive_frame(world.registry);
    const ViewDraws draws;
    for (const auto unit : {slots().Friend, slots().Enemy}) {
        const Vector3 at     = world.position(unit);
        const Vector3 marker = Vector3Add(at, kMarkerOffset);
        CHECK(drawn(draws.main, at));
        CHECK_FALSE(drawn(draws.main, marker));
        CHECK(drawn(draws.minimap, marker));
        CHECK_FALSE(drawn(draws.minimap, at));
    }
}

TEST_CASE("minimap: an orthographic view shows the same size at every depth", "[runtime][minimap][camera-view]") {
    World world;
    const auto& view = world.view(slots().Minimap);
    const auto width_at = [&](float height) {
        const auto left  = cam::to_screen(view, Vector3{.x = 0.0F, .y = height, .z = 0.0F});
        const auto right = cam::to_screen(view, Vector3{.x = 1.0F, .y = height, .z = 0.0F});
        REQUIRE(left.in_front);
        REQUIRE(right.in_front);
        return right.position.x - left.position.x;
    };
    // 30 world units fill the 180-pixel-high viewport.
    CHECK(width_at(0.0F) == Catch::Approx(6.0F));
    CHECK(width_at(10.0F) == Catch::Approx(6.0F));
    const auto behind = cam::to_screen(view, Vector3{.x = 0.0F, .y = 45.0F, .z = 0.0F});
    CHECK_FALSE(behind.in_front);
}

TEST_CASE("minimap: an orthographic ray through a pixel projects back to that pixel",
          "[runtime][minimap][camera-view]") {
    World world;
    const auto& view = world.view(slots().Minimap);
    for (float u = 0.0F; u <= 1.0F; u += 0.125F) {
        for (float v = 0.0F; v <= 1.0F; v += 0.125F) {
            const Vector2 pixel{.x = view.rect_min.x + (u * view.rect_size.x),
                                .y = view.rect_min.y + (v * view.rect_size.y)};
            const auto ray = cam::ray(view, pixel);
            CHECK(fake::approx_equal(ray.direction, view.forward));
            CHECK(Vector3DotProduct(Vector3Subtract(ray.origin, view.position), view.forward) ==
                  Catch::Approx(view.near).margin(1e-4));
            for (const float d : {0.01F, 1.0F, 10.0F, 39.0F}) {
                const auto point     = Vector3Add(ray.origin, Vector3Scale(ray.direction, d));
                const auto projected = cam::to_screen(view, point);
                CHECK(projected.in_front);
                CHECK(Vector2Distance(projected.position, pixel) < 0.01F);
                const auto back = cam::ray(view, projected.position);
                CHECK(Vector3Length(Vector3CrossProduct(Vector3Subtract(point, back.origin), back.direction)) <
                      0.01F);
            }
        }
    }
}

TEST_CASE("minimap: to_screen agrees with the orthographic renderer", "[runtime][minimap][camera-view]") {
    World world(true);
    const Vector3 eye = world.position(slots().Minimap);
    fake::clear_call_log();
    drive_frame(world.registry);

    std::optional<Camera3D> camera;
    std::optional<fake::RecordedViewport> viewport;
    std::optional<Matrix> projection;
    for (const auto& call : fake::call_log()) {
        if (const auto* begin = std::get_if<fake::RecordedBeginMode3D>(&call)) {
            camera.reset();
            viewport.reset();
            projection.reset();
            if (fake::approx_equal(begin->camera.position, eye)) {
                camera = begin->camera;
            }
        } else if (const auto* recorded = std::get_if<fake::RecordedViewport>(&call);
                   recorded != nullptr && camera.has_value() && !viewport.has_value()) {
            viewport = *recorded;
        } else if (const auto* matrix = std::get_if<fake::RecordedSetMatrixProjection>(&call);
                   matrix != nullptr && camera.has_value() && !projection.has_value()) {
            projection = matrix->projection;
        }
        if (camera.has_value() && viewport.has_value() && projection.has_value()) {
            break;
        }
    }
    REQUIRE(camera.has_value());
    REQUIRE(viewport.has_value());
    REQUIRE(projection.has_value());
    CHECK(camera->projection == CAMERA_ORTHOGRAPHIC);

    const auto& view = world.view(slots().Minimap);
    for (const auto unit : {slots().Friend, slots().Enemy}) {
        const Vector3 point = world.position(unit);
        Quaternion clip{.x = point.x, .y = point.y, .z = point.z, .w = 1.0F};
        clip = QuaternionTransform(clip, MatrixLookAt(camera->position, camera->target, camera->up));
        clip = QuaternionTransform(clip, *projection);
        const Vector2 rendered{
            .x = static_cast<float>(viewport->x) + (((clip.x / clip.w) + 1.0F) * 0.5F * static_cast<float>(viewport->width)),
            .y = kScreenHeight - (static_cast<float>(viewport->y) +
                                  (((clip.y / clip.w) + 1.0F) * 0.5F * static_cast<float>(viewport->height))),
        };
        const auto projected = cam::to_screen(view, point);
        CHECK(projected.in_front);
        CHECK(Vector2Distance(projected.position, rendered) <= 0.5F);
    }
}

TEST_CASE("minimap: a click on the minimap moves the camera target to the ground under it",
          "[runtime][minimap][camera-view]") {
    World world;
    const Vector3 enemy = world.position(slots().Enemy);
    auto& click         = world.registry.get<Click>(slots().Mouse);
    click.pixel         = cam::to_screen(world.view(slots().Minimap), enemy).position;
    click.pressed       = true;
    drive_frame(world.registry);
    const Vector3 target = world.registry.get<CameraTarget>(slots().Main).point;
    CHECK(Vector3Distance(target, enemy) < 0.01F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
