// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <entt/entt.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

float length(Vector3 v) {
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
}

Vector3 subtract(Vector3 a, Vector3 b) {
    return Vector3{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

using CreationOrdinal = cactus::runtime::entt_backend::CreationOrdinal;

// spatial-broadphase-runtime: forces every SapBroadPhase2D/3D instance
// constructed while this guard is alive to a fixed small-domain threshold —
// generated code constructs its broad-phase instance locally with no handle
// a test could otherwise reach. Resets to "no override" (each instance's own
// default) on scope exit, since the override is a process-global test hook
// shared across every TEST_CASE in this binary.
struct ScopedSapThresholdOverride {
    explicit ScopedSapThresholdOverride(std::size_t threshold) {
        cactus::runtime::entt_backend::set_sap_small_domain_threshold_override_for_testing(threshold);
    }
    ~ScopedSapThresholdOverride() {
        cactus::runtime::entt_backend::set_sap_small_domain_threshold_override_for_testing(std::nullopt);
    }
    ScopedSapThresholdOverride(const ScopedSapThresholdOverride&)            = delete;
    ScopedSapThresholdOverride& operator=(const ScopedSapThresholdOverride&) = delete;
};

constexpr std::size_t kForcedBruteForceThreshold = 1'000'000;
constexpr std::size_t kForcedSapThreshold         = 0;

std::vector<entt::entity> balls_by_creation_order(entt::registry& registry) {
    auto view =
        registry.view<main__Ball, main__SphereCollider, std_transform_volume__WorldTransform, CreationOrdinal>();
    std::vector<std::pair<std::uint64_t, entt::entity>> ordered;
    for (auto entity : view) {
        ordered.emplace_back(registry.get<CreationOrdinal>(entity).value, entity);
    }
    std::ranges::sort(ordered);
    std::vector<entt::entity> result;
    result.reserve(ordered.size());
    for (const auto& [ordinal, entity] : ordered) {
        result.push_back(entity);
    }
    return result;
}

std::vector<Vector3> ball_velocities_by_creation_order(entt::registry& registry) {
    std::vector<Vector3> velocities;
    for (auto entity : balls_by_creation_order(registry)) {
        velocities.push_back(registry.get<main__Ball>(entity).velocity);
    }
    return velocities;
}

// Manually creates a ball with just the three traits DetectBallContact's
// pair binding requires (Ball, SphereCollider, WorldTransform) — skipping
// BallTemplate's meshes.Renderer, which no physics rule reads and headless
// rendering never visits. Used instead of the real click-to-spawn input path
// so both compared registries reach an identical post-spawn state without
// depending on camera/screen-to-plane projection maths.
entt::entity spawn_ball(entt::registry& registry, Vector3 position, Vector3 velocity, float radius) {
    const auto entity = registry.create();
    registry.emplace<CreationOrdinal>(
        entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
    registry.emplace<std_transform_volume__WorldTransform>(entity,
                                                           std_transform_volume__WorldTransform{.position = position});
    registry.emplace<main__Ball>(entity, main__Ball{.velocity = velocity});
    registry.emplace<main__SphereCollider>(entity, main__SphereCollider{.radius = radius});
    return entity;
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

// spatial-broadphase-runtime: DetectBallContact is SAP-eligible (its where:
// clause is a direct spheres_overlap call over two Ball/SphereCollider/
// WorldTransform bindings). These tests force the runtime broad phase's
// internal small-domain threshold to either always use its brute-force
// fallback or always use the swept path, and confirm ball-ball contact
// resolution is bit-for-bit identical either way — the correctness bar
// "Execution strategy does not affect tuple membership or order"
// (dsl-pair-relations) exercised against the real compiled example instead
// of a synthetic fixture.

TEST_CASE("example-bouncy-balls-3d: forced brute-force and forced SAP execution produce identical ball velocities "
         "for the original eight-ball layout",
         "[runtime][codegen-entt][example][bouncy-balls-3d][spatial-join]") {
    entt::registry brute_force_registry;
    {
        ScopedSapThresholdOverride guard(kForcedBruteForceThreshold);
        cactus::runtime::entt_backend::generated_init_project(brute_force_registry);
        cactus::runtime::entt_backend::generated_load_project(brute_force_registry);
        cactus_headless_test::dispatch_load_event(brute_force_registry);
        for (int frame = 0; frame < 10; ++frame) {
            cactus_headless_test::drive_frame(brute_force_registry);
        }
    }

    entt::registry sap_registry;
    {
        ScopedSapThresholdOverride guard(kForcedSapThreshold);
        cactus::runtime::entt_backend::generated_init_project(sap_registry);
        cactus::runtime::entt_backend::generated_load_project(sap_registry);
        cactus_headless_test::dispatch_load_event(sap_registry);
        for (int frame = 0; frame < 10; ++frame) {
            cactus_headless_test::drive_frame(sap_registry);
        }
    }

    const auto brute_force_velocities = ball_velocities_by_creation_order(brute_force_registry);
    const auto sap_velocities         = ball_velocities_by_creation_order(sap_registry);
    REQUIRE(brute_force_velocities.size() == 8);
    REQUIRE(sap_velocities.size() == 8);
    for (std::size_t i = 0; i < brute_force_velocities.size(); ++i) {
        CHECK(cactus_raylib_fake::approx_equal(brute_force_velocities[i], sap_velocities[i]));
    }
}

TEST_CASE("example-bouncy-balls-3d: forced brute-force and forced SAP execution still agree after dynamic spawning",
         "[runtime][codegen-entt][example][bouncy-balls-3d][spatial-join]") {
    constexpr Vector3 kSpawnPosition{.x = 0.2F, .y = 0.1F, .z = -0.3F};
    constexpr Vector3 kSpawnVelocity{.x = -0.5F, .y = 0.3F, .z = 0.4F};
    constexpr float kSpawnRadius = 0.35F;

    entt::registry brute_force_registry;
    {
        ScopedSapThresholdOverride guard(kForcedBruteForceThreshold);
        cactus::runtime::entt_backend::generated_init_project(brute_force_registry);
        cactus::runtime::entt_backend::generated_load_project(brute_force_registry);
        cactus_headless_test::dispatch_load_event(brute_force_registry);
        for (int frame = 0; frame < 5; ++frame) {
            cactus_headless_test::drive_frame(brute_force_registry);
        }
        spawn_ball(brute_force_registry, kSpawnPosition, kSpawnVelocity, kSpawnRadius);
        for (int frame = 0; frame < 10; ++frame) {
            cactus_headless_test::drive_frame(brute_force_registry);
        }
    }

    entt::registry sap_registry;
    {
        ScopedSapThresholdOverride guard(kForcedSapThreshold);
        cactus::runtime::entt_backend::generated_init_project(sap_registry);
        cactus::runtime::entt_backend::generated_load_project(sap_registry);
        cactus_headless_test::dispatch_load_event(sap_registry);
        for (int frame = 0; frame < 5; ++frame) {
            cactus_headless_test::drive_frame(sap_registry);
        }
        spawn_ball(sap_registry, kSpawnPosition, kSpawnVelocity, kSpawnRadius);
        for (int frame = 0; frame < 10; ++frame) {
            cactus_headless_test::drive_frame(sap_registry);
        }
    }

    const auto brute_force_velocities = ball_velocities_by_creation_order(brute_force_registry);
    const auto sap_velocities         = ball_velocities_by_creation_order(sap_registry);
    REQUIRE(brute_force_velocities.size() == 9);
    REQUIRE(sap_velocities.size() == 9);
    for (std::size_t i = 0; i < brute_force_velocities.size(); ++i) {
        CHECK(cactus_raylib_fake::approx_equal(brute_force_velocities[i], sap_velocities[i]));
    }
}

// Hidden ([.]) so ctest's default run stays fast; run explicitly with
// `test_bouncy_balls_3d_headless_behavior.exe "[benchmark]"`. Measures total
// pair-handler activation time (proxy sync + candidate generation + residual
// predicate/body evaluation, all inlined into one generated function call) for
// the real DetectBallContact rule, forced brute-force vs. forced SAP, across
// the same representative ball counts as the runtime-level candidate-
// generation benchmark (test_runtime_sap_broadphase.cpp) — that file isolates
// candidate-generation cost alone; this one is the end-to-end confirmation
// against the compiled example. Balls are spawned directly (bypassing
// SpawnInitialBalls' load-time RNG draw) so the count is exact.
TEST_CASE("example-bouncy-balls-3d: total pair-handler activation time benchmark, brute-force vs SAP across "
         "representative ball counts",
         "[.][benchmark][runtime][codegen-entt][example][bouncy-balls-3d][spatial-join]") {
    for (const std::size_t count :
        {std::size_t{8}, std::size_t{32}, std::size_t{64}, std::size_t{128}, std::size_t{256}, std::size_t{512},
         std::size_t{1024}}) {
        entt::registry registry;
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);

        for (std::size_t i = 0; i < count; ++i) {
            const auto t = static_cast<float>(i);
            const Vector3 position{
                .x = std::fmod(t * 0.37F, 6.0F) - 3.0F, .y = std::fmod(t * 0.53F, 6.0F) - 3.0F,
                .z = std::fmod(t * 0.71F, 6.0F) - 3.0F};
            const Vector3 velocity{.x = std::sin(t), .y = std::cos(t), .z = std::sin(t * 0.5F)};
            spawn_ball(registry, position, velocity, 0.3F);
        }

        const cactus::runtime::entt_backend::std_core__fixed_tickPhaseRuntimeState trigger{.dt = 1.0F / 60.0F};

        cactus::runtime::entt_backend::set_sap_small_domain_threshold_override_for_testing(kForcedBruteForceThreshold);
        BENCHMARK("brute-force total activation, N=" + std::to_string(count)) {
            main__detect_ball_contact_fixed_tick(registry, trigger);
        };

        cactus::runtime::entt_backend::set_sap_small_domain_threshold_override_for_testing(kForcedSapThreshold);
        BENCHMARK("SAP total activation, N=" + std::to_string(count)) {
            main__detect_ball_contact_fixed_tick(registry, trigger);
        };

        cactus::runtime::entt_backend::set_sap_small_domain_threshold_override_for_testing(std::nullopt);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
