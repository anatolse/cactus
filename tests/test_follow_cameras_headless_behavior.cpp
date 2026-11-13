// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace volume = cactus_gen::stdlib::camera::volume;
namespace flat   = cactus_gen::stdlib::camera::flat;
using Pose3      = cactus_gen::stdlib::transform::volume::WorldTransform;
using Pose2      = cactus_gen::stdlib::transform::flat::WorldTransform;

// The unit vector a rotation turns -Z into.
Vector3 forward_of(Quaternion rotation) {
    return Vector3RotateByQuaternion(Vector3{.x = 0.0F, .y = 0.0F, .z = -1.0F}, rotation);
}

Vector3 direction(Vector3 from, Vector3 to) {
    return Vector3Normalize(Vector3Subtract(to, from));
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("follow cameras track their targets through required links", "[runtime][required-links][camera]") {
    entt::registry registry;
    cactus::runtime::entt_backend::clear_runtime_faults();
    cactus_headless_test::init_and_load(registry);
    const auto& slots   = cactus_gen::detail::generated_named_slots().follow_cameras;
    const auto hero     = slots.Hero;
    const auto chaser   = slots.Chaser;
    const auto orbiter  = slots.Orbiter;
    const auto watcher  = slots.Watcher;
    const auto sprite   = slots.Sprite;
    const auto overview = slots.Overview;

    cactus_headless_test::drive_frame(registry);

    {  // a 3D follow camera sits at its offset from the target and looks at it
        const auto target = registry.get<Pose3>(hero).position;
        const auto& pose  = registry.get<Pose3>(chaser);
        const auto offset = registry.get<volume::FollowCamera>(chaser).offset;
        CHECK(pose.position.x == Catch::Approx(target.x + offset.x));
        CHECK(pose.position.y == Catch::Approx(target.y + offset.y));
        CHECK(pose.position.z == Catch::Approx(target.z + offset.z));
        const auto looking = forward_of(pose.rotation);
        const auto wanted  = direction(pose.position, target);
        CHECK(looking.x == Catch::Approx(wanted.x).margin(1e-4));
        CHECK(looking.y == Catch::Approx(wanted.y).margin(1e-4));
        CHECK(looking.z == Catch::Approx(wanted.z).margin(1e-4));
    }

    {  // a third-person camera stays behind the target's facing, above it
        const auto& target = registry.get<Pose3>(hero);
        const auto& orbit  = registry.get<volume::ThirdPersonCamera>(orbiter);
        const auto& pose   = registry.get<Pose3>(orbiter);
        const auto behind  = Vector3Scale(forward_of(target.rotation), -orbit.distance);
        CHECK(pose.position.x == Catch::Approx(target.position.x + behind.x));
        CHECK(pose.position.y == Catch::Approx(target.position.y + orbit.height + behind.y));
        CHECK(pose.position.z == Catch::Approx(target.position.z + behind.z));
    }

    {  // a 2D follow camera centers its offset on the target
        const auto target = registry.get<Pose2>(sprite).position;
        const auto& cam   = registry.get<flat::Camera>(overview);
        CHECK(cam.offset.x == Catch::Approx(target.x));
        CHECK(cam.offset.y == Catch::Approx(target.y + 1.0F));
    }

    for (int frame = 0; frame < 4; ++frame) {
        cactus_headless_test::drive_frame(registry);
    }

    {  // the bait burned out: its watcher stopped following and kept its place
        REQUIRE_FALSE(registry.valid(slots.Bait));
        CHECK_FALSE(registry.all_of<volume::FollowCamera>(watcher));
        CHECK(registry.all_of<Pose3>(watcher));
        CHECK(registry.all_of<volume::FollowCamera>(chaser));
        CHECK(cactus::runtime::entt_backend::runtime_fault_count() == 0);
    }

    {  // the others keep following the moving hero
        const auto before = registry.get<Pose3>(chaser).position.x;
        cactus_headless_test::drive_frame(registry);
        CHECK(registry.get<Pose3>(chaser).position.x > before);
        CHECK(registry.get<Pose3>(chaser).position.x ==
              Catch::Approx(registry.get<Pose3>(hero).position.x + registry.get<volume::FollowCamera>(chaser).offset.x));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
