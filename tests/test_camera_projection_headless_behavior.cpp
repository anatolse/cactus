// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using cactus_gen::camera_projection::Drag;
using cactus_gen::camera_projection::InBox;
using cactus_gen::stdlib::transform::volume::WorldTransform;
using cactus_headless_test::drive_frame;

constexpr Rectangle kViewport{.x = 0.0F, .y = 0.0F, .width = 640.0F, .height = 720.0F};

auto& slots() {
    return cactus_gen::detail::generated_named_slots().camera_projection;
}

struct World {
    entt::registry registry;

    World() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_screen_size(1280, 720);
        cactus_headless_test::init_and_load(registry);
        drive_frame(registry);
        drive_frame(registry);
    }

    [[nodiscard]] bool in_box(entt::entity unit) const { return registry.all_of<InBox>(unit); }

    // Where the renderer draws a unit, with raylib's own projection into the viewport.
    [[nodiscard]] Vector2 drawn_at(entt::entity unit) const {
        const auto& pose      = registry.get<WorldTransform>(slots().Overview);
        const Camera3D camera = cactus::runtime::entt_backend::camera_3d_from_pose(pose.position, pose.rotation, 60.0F);
        return GetWorldToScreenEx(registry.get<WorldTransform>(unit).position,
                                  camera,
                                  static_cast<int>(kViewport.width),
                                  static_cast<int>(kViewport.height));
    }
};

bool inside(Vector2 point, const Drag& drag) {
    return point.x >= drag.min.x && point.x <= drag.max.x && point.y >= drag.min.y && point.y <= drag.max.y &&
           CheckCollisionPointRec(point, kViewport);
}

}  // namespace

TEST_CASE("A drag rectangle keeps InBox on exactly the units drawn inside it", "[runtime][camera-projection]") {
    World world;
    const auto& drag = world.registry.get<Drag>(slots().Selection);

    for (const auto unit : {slots().Center, slots().Near, slots().Beside, slots().Far}) {
        CHECK(world.in_box(unit) == inside(world.drawn_at(unit), drag));
    }
    CHECK(world.in_box(slots().Center));
    CHECK(world.in_box(slots().Near));
    CHECK_FALSE(world.in_box(slots().Far));

    // Inside the rectangle's pixels, but right of the viewport.
    const Vector2 beside = world.drawn_at(slots().Beside);
    CHECK(beside.x > kViewport.width);
    CHECK(beside.x < drag.max.x);
    CHECK_FALSE(world.in_box(slots().Beside));

    // Behind the camera, where a projection without a depth test lands in the box.
    CHECK(inside(world.drawn_at(slots().Behind), drag));
    CHECK_FALSE(world.in_box(slots().Behind));
}

TEST_CASE("Moving the drag rectangle away removes InBox", "[runtime][camera-projection]") {
    World world;
    REQUIRE(world.in_box(slots().Center));
    auto& drag = world.registry.get<Drag>(slots().Selection);
    drag.min   = Vector2{.x = 0.0F, .y = 0.0F};
    drag.max   = Vector2{.x = 10.0F, .y = 10.0F};
    drive_frame(world.registry);
    CHECK(world.registry.view<InBox>().empty());
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
