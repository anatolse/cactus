// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <numbers>
#include <random>
#include <string>

namespace {

using cactus::runtime::entt_backend::CreationOrdinal;
using Actor    = cross_domain_spatial_join_runtime__Actor;
using Solid    = cross_domain_spatial_join_runtime__Solid;
using Recorder = cross_domain_spatial_join_runtime__Recorder;

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

entt::entity create_entity(entt::registry& registry) {
    const auto entity = registry.create();
    registry.emplace<CreationOrdinal>(
        entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
    return entity;
}

// 300 spheres against 300 boxes in a 20-unit cube; every fifth sphere is also
// a box, so shared entities produce self tuples.
void populate_scene(entt::registry& registry, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> position(-10.0F, 10.0F);
    std::uniform_real_distribution<float> radius(0.1F, 1.2F);
    std::uniform_real_distribution<float> extent(0.2F, 3.0F);
    std::uniform_real_distribution<float> angle(0.0F, 2.0F * std::numbers::pi_v<float>);
    const auto random_vec3 = [&](auto& distribution) {
        return Vector3{.x = distribution(rng), .y = distribution(rng), .z = distribution(rng)};
    };
    const auto random_rotation = [&] {
        const auto axis = Vector3Normalize(random_vec3(position));
        return cactus::runtime::stdlib::math::quat::from_axis_angle(axis, angle(rng));
    };
    for (int tag = 1; tag <= 300; ++tag) {
        const auto entity = create_entity(registry);
        const auto center = random_vec3(position);
        registry.emplace<Actor>(entity, Actor{.pos = center, .radius = radius(rng), .tag = tag});
        if (tag % 5 == 0) {
            registry.emplace<Solid>(entity,
                                    Solid{.pos = center, .size = random_vec3(extent), .rotation = random_rotation(), .tag = tag});
        }
    }
    for (int tag = 301; tag <= 540; ++tag) {
        registry.emplace<Solid>(create_entity(registry),
                                Solid{.pos = random_vec3(position), .size = random_vec3(extent), .rotation = random_rotation(), .tag = tag});
    }
}

const Recorder& recorder(entt::registry& registry) {
    const auto view = registry.view<Recorder>();
    REQUIRE(view.size() == 1);
    return registry.get<Recorder>(view.front());
}

}  // namespace

TEST_CASE("Accelerated rules are compiled to the bipartite broad phase", "[runtime][codegen-entt][spatial-join]") {
    std::ifstream generated(CACTUS_HEADLESS_GENERATED_CPP);
    const std::string code{std::istreambuf_iterator<char>(generated), std::istreambuf_iterator<char>()};
    std::size_t broad_phases = 0;
    for (auto at = code.find("SapBroadPhase3D __sap;"); at != std::string::npos;
         at      = code.find("SapBroadPhase3D __sap;", at + 1)) {
        ++broad_phases;
    }
    CHECK(broad_phases == 2);
}

TEST_CASE("A cross-domain sphere-box pass executes the same tuples as the unaccelerated pass",
          "[runtime][codegen-entt][spatial-join]") {
    for (const std::uint32_t seed : {1U, 2U, 3U}) {
        entt::registry registry;
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);
        populate_scene(registry, seed);

        drive_frame(registry);

        const auto& log = recorder(registry);
        CHECK(log.accelerated_count > 50);
        CHECK(log.accelerated_count == log.plain_count);
        CHECK(log.accelerated_hash == log.plain_hash);
        CHECK(log.nearest_accelerated_count > 50);
        CHECK(log.nearest_accelerated_count == log.nearest_plain_count);
        CHECK(log.nearest_accelerated_hash == log.nearest_plain_hash);
    }
}

TEST_CASE("A sphere touching only the rotated corner of a box is not missed", "[runtime][codegen-entt][spatial-join]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    const auto rotation = cactus::runtime::stdlib::math::quat::from_axis_angle(
        Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, std::numbers::pi_v<float> / 4);
    registry.emplace<Solid>(create_entity(registry),
                            Solid{.pos = Vector3{}, .size = Vector3{.x = 2.0F, .y = 2.0F, .z = 2.0F}, .rotation = rotation, .tag = 7});
    // The rotated corner reaches x = sqrt(2); the unrotated box would end at x = 1.
    registry.emplace<Actor>(create_entity(registry),
                            Actor{.pos = Vector3{.x = 1.55F, .y = 0.0F, .z = 0.0F}, .radius = 0.2F, .tag = 1});

    drive_frame(registry);

    const auto& log = recorder(registry);
    CHECK(log.nearest_accelerated_count == 1);
    CHECK(log.nearest_accelerated_hash == 1007);
    CHECK(log.accelerated_count == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
