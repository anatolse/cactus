// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <entt/entt.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace {

float length(Vector3 v) {
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
}

Vector3 subtract(Vector3 a, Vector3 b) {
    return Vector3{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

}  // namespace

// dsl-where-clause / stdlib-collision migration: DetectBallContact now
// expresses its self-pair, overlap, and approach rejection via `where:`
// instead of leading `if ... return` statements (see main.cactus). These
// tests exercise the real compiled example rule, not a synthetic fixture, to
// confirm that migration is behavior-preserving per
// openspec/specs/example-bouncy-balls-3d/spec.md's ball-ball contact
// requirement.

TEST_CASE("example-bouncy-balls-3d: initial balls stay within the box over many frames",
          "[runtime][codegen-entt][example][bouncy-balls-3d]") {
    // General regression check mirroring test_bouncy_bubbles_headless_behavior's
    // containment style: if the where:-based DetectBallContact (or
    // DetectWallContact, unchanged) mis-rejected a tuple it should have kept,
    // energy would blow up and a ball would escape the box within a few
    // hundred frames.
    constexpr float kBound     = 3.0F + 1.0F;  // box half-size 3.0 + generous margin
    constexpr int kFrameCount  = 300;          // 5 simulated seconds at 60 fps

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus_headless_test::dispatch_load_event(registry);

    const auto view = registry.view<main__Ball, std_transform_volume__WorldTransform>();
    std::size_t ball_count = 0;
    for (auto entity : view) {
        (void)entity;
        ++ball_count;
    }
    REQUIRE(ball_count == 8);

    for (int frame = 0; frame < kFrameCount; ++frame) {
        cactus_headless_test::drive_frame(registry);
        for (auto entity : view) {
            const auto& position = registry.get<std_transform_volume__WorldTransform>(entity).position;
            INFO("frame " << frame << " position (" << position.x << ", " << position.y << ", " << position.z << ")");
            CHECK(std::abs(position.x) <= kBound);
            CHECK(std::abs(position.y) <= kBound);
            CHECK(std::abs(position.z) <= kBound);
        }
    }
}

TEST_CASE("example-bouncy-balls-3d: ball-ball contact rejects self/non-overlapping pairs, emits both sides when "
          "approaching and overlapping, and imparts more change on the lighter ball",
          "[runtime][codegen-entt][example][bouncy-balls-3d][where-clause]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus_headless_test::dispatch_load_event(registry);

    // SpawnInitialBalls places its 8 balls at fixed, deterministic corners
    // (only radius/velocity/color are randomized); find the two at corner
    // k=0 (-1.5,-1.2,-1.5) and k=1 (1.5,-1.2,-1.5) to commandeer for a
    // controlled head-on collision, leaving the other 6 as spawned.
    const auto view = registry.view<main__Ball, main__SphereCollider, std_transform_volume__WorldTransform>();
    std::optional<entt::entity> heavy_entity;
    std::optional<entt::entity> light_entity;
    for (auto entity : view) {
        const auto& position = registry.get<std_transform_volume__WorldTransform>(entity).position;
        if (cactus_raylib_fake::approx_equal(position, Vector3{.x = -1.5F, .y = -1.2F, .z = -1.5F})) {
            heavy_entity = entity;
        } else if (cactus_raylib_fake::approx_equal(position, Vector3{.x = 1.5F, .y = -1.2F, .z = -1.5F})) {
            light_entity = entity;
        }
    }
    REQUIRE(heavy_entity.has_value());
    REQUIRE(light_entity.has_value());
    const entt::entity heavy = *heavy_entity;
    const entt::entity light = *light_entity;
    REQUIRE(heavy != light);

    // Overlapping (dist 1.0 < radius sum 1.3) and approaching (moving
    // directly toward each other along +/-x).
    registry.get<main__SphereCollider>(heavy).radius = 1.0F;
    registry.get<main__SphereCollider>(light).radius = 0.3F;
    registry.get<std_transform_volume__WorldTransform>(heavy).position = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    registry.get<std_transform_volume__WorldTransform>(light).position = Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const Vector3 heavy_velocity_before = Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const Vector3 light_velocity_before = Vector3{.x = -1.0F, .y = 0.0F, .z = 0.0F};
    registry.get<main__Ball>(heavy).velocity = heavy_velocity_before;
    registry.get<main__Ball>(light).velocity = light_velocity_before;

    // Snapshot every other ball's velocity — none of them overlap each
    // other or the two controlled balls, so a correct self-pair/
    // non-overlap rejection leaves them all untouched this frame.
    std::vector<std::pair<entt::entity, Vector3>> other_velocities_before;
    for (auto entity : view) {
        if (entity != heavy && entity != light) {
            other_velocities_before.emplace_back(entity, registry.get<main__Ball>(entity).velocity);
        }
    }
    REQUIRE(other_velocities_before.size() == 6);

    cactus_headless_test::drive_frame(registry);

    for (const auto& [entity, velocity_before] : other_velocities_before) {
        const auto& velocity_after = registry.get<main__Ball>(entity).velocity;
        CHECK(cactus_raylib_fake::approx_equal(velocity_after, velocity_before));
    }

    // Mirror DetectBallContact's own mass-weighted (radius^2) 1D-along-normal
    // elastic solve to compute the expected resolved velocities, rather than
    // hand-computing decimals that could themselves be miscalculated.
    const float mass_heavy = 1.0F * 1.0F;
    const float mass_light = 0.3F * 0.3F;
    const Vector3 normal_for_heavy =
        Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F};  // (light_pos - heavy_pos) / dist, dist == 1.0
    const Vector3 normal_for_light = Vector3{.x = -1.0F, .y = 0.0F, .z = 0.0F};
    const auto dot                = [](Vector3 a, Vector3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); };
    const auto resolve             = [&](Vector3 vel_self,
                              float mass_self,
                              Vector3 vel_other,
                              float mass_other,
                              Vector3 normal) {
        const float v1n       = dot(vel_self, normal);
        const float v2n       = dot(vel_other, normal);
        const float v1n_prime = (((mass_self - mass_other) * v1n) + (2.0F * mass_other * v2n)) / (mass_self + mass_other);
        const float delta     = v1n_prime - v1n;
        return Vector3{
            .x = vel_self.x + (normal.x * delta), .y = vel_self.y + (normal.y * delta), .z = vel_self.z + (normal.z * delta)};
    };
    const Vector3 expected_heavy_velocity =
        resolve(heavy_velocity_before, mass_heavy, light_velocity_before, mass_light, normal_for_heavy);
    const Vector3 expected_light_velocity =
        resolve(light_velocity_before, mass_light, heavy_velocity_before, mass_heavy, normal_for_light);

    const Vector3 heavy_velocity_after = registry.get<main__Ball>(heavy).velocity;
    const Vector3 light_velocity_after = registry.get<main__Ball>(light).velocity;
    CHECK(cactus_raylib_fake::approx_equal(heavy_velocity_after, expected_heavy_velocity, 0.001F));
    CHECK(cactus_raylib_fake::approx_equal(light_velocity_after, expected_light_velocity, 0.001F));

    // Both sides received a targeted bounce (approaching, overlapping pair
    // emits one event per side): both velocities actually changed.
    CHECK_FALSE(cactus_raylib_fake::approx_equal(heavy_velocity_after, heavy_velocity_before));
    CHECK_FALSE(cactus_raylib_fake::approx_equal(light_velocity_after, light_velocity_before));

    // Heavier ball (radius 1.0, mass 1.0) imparts more change on the
    // lighter ball (radius 0.3, mass 0.09) than vice versa.
    const float heavy_change = length(subtract(heavy_velocity_after, heavy_velocity_before));
    const float light_change = length(subtract(light_velocity_after, light_velocity_before));
    CHECK(light_change > heavy_change);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
