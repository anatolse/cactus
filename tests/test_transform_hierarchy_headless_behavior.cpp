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

using World    = std_transform_volume__WorldTransform;
namespace quat = cactus::runtime::stdlib::math::quat;

Catch::Approx near(float value) {
    return Catch::Approx(value).margin(1e-5);
}

template <typename Marker>
entt::entity only(entt::registry& registry) {
    auto view = registry.view<Marker>();
    REQUIRE(view.size() == 1);
    return *view.begin();
}

const World& world_of(entt::registry& registry, entt::entity entity) {
    return registry.get<World>(entity);
}

void check_position(const World& world, float x, float y, float z) {
    CHECK(world.position.x == near(x));
    CHECK(world.position.y == near(y));
    CHECK(world.position.z == near(z));
}

void check_rotation(Quat actual, Quat expected) {
    CHECK(actual.x == near(expected.x));
    CHECK(actual.y == near(expected.y));
    CHECK(actual.z == near(expected.z));
    CHECK(actual.w == near(expected.w));
}

float length_squared(Quat q) {
    return (q.x * q.x) + (q.y * q.y) + (q.z * q.z) + (q.w * q.w);
}

struct Loaded {
    entt::registry registry;

    Loaded() {
        cactus_headless_test::init_and_load(registry);
    }
};

}  // namespace

TEST_CASE("a child composes onto a pose root", "[runtime][codegen-entt][stdlib-hierarchical-transforms]") {
    Loaded loaded;
    auto& registry = loaded.registry;
    cactus_headless_test::drive_frame(registry);

    check_position(world_of(registry, only<transform_hierarchy__Feet>(registry)), 5.0F, -1.0F, 0.0F);
    check_position(world_of(registry, generated_named_slots().transform_hierarchy__StillRoot), 5.0F, 0.0F, 0.0F);
}

TEST_CASE("a child without LocalTransform is not propagated",
          "[runtime][codegen-entt][stdlib-hierarchical-transforms]") {
    Loaded loaded;
    auto& registry = loaded.registry;
    cactus_headless_test::drive_frame(registry);

    check_position(world_of(registry, only<transform_hierarchy__Badge>(registry)), 9.0F, 9.0F, 9.0F);
}

TEST_CASE("nested children compose through a pose root", "[runtime][codegen-entt][stdlib-hierarchical-transforms]") {
    Loaded loaded;
    auto& registry = loaded.registry;
    cactus_headless_test::drive_frame(registry);

    const Quat root_rotation   = quat::from_euler(0.0F, 0.7F, 0.0F);
    const Quat arm_rotation    = quat::compose(root_rotation, quat::from_euler(0.9F, 0.0F, 0.0F));
    const Quat finger_rotation = quat::compose(arm_rotation, quat::from_euler(0.0F, 0.0F, 1.1F));

    const auto& arm    = world_of(registry, only<transform_hierarchy__Arm>(registry));
    const auto& finger = world_of(registry, only<transform_hierarchy__Finger>(registry));
    check_position(arm, 1.0F, 2.0F, 0.0F);
    check_position(finger, 1.0F, 3.0F, 0.0F);
    check_rotation(arm.rotation, arm_rotation);
    check_rotation(finger.rotation, finger_rotation);
    CHECK(length_squared(finger.rotation) == Catch::Approx(1.0F));

    const auto& root = world_of(registry, generated_named_slots().transform_hierarchy__TurnedRoot);
    check_rotation(root.rotation, root_rotation);
}

TEST_CASE("a child of a stale parent derives from its own LocalTransform",
          "[runtime][codegen-entt][stdlib-hierarchical-transforms]") {
    Loaded loaded;
    auto& registry = loaded.registry;
    registry.destroy(generated_named_slots().transform_hierarchy__StillRoot);
    cactus_headless_test::drive_frame(registry);

    check_position(world_of(registry, only<transform_hierarchy__Feet>(registry)), 0.0F, -1.0F, 0.0F);
}

TEST_CASE("a pose root moved in late_tick carries its child the same late_tick",
          "[runtime][codegen-entt][stdlib-hierarchical-transforms]") {
    Loaded loaded;
    auto& registry = loaded.registry;
    for (int frame = 1; frame <= 3; ++frame) {
        cactus_headless_test::drive_frame(registry);
        const float root_x = -10.0F + static_cast<float>(frame);
        check_position(world_of(registry, generated_named_slots().transform_hierarchy__MovingRoot), root_x, 0.0F, 0.0F);
        check_position(world_of(registry, only<transform_hierarchy__Rider>(registry)), root_x, 3.0F, 0.0F);
    }
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
