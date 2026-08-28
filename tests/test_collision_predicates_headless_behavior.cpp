// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

}  // namespace

TEST_CASE("stdlib-collision: circles_overlap and spheres_overlap distinguish overlapping/touching/separated",
          "[runtime][codegen-entt][stdlib-collision]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    drive_frame(registry);

    const auto view = registry.view<collision_predicates_runtime__Probe>();
    REQUIRE(view.size() == 1);
    const auto& probe = registry.get<collision_predicates_runtime__Probe>(*view.begin());

    CHECK(probe.circle_overlap);
    CHECK_FALSE(probe.circle_touch);
    CHECK_FALSE(probe.circle_separate);
    CHECK(probe.sphere_overlap);
    CHECK_FALSE(probe.sphere_touch);
    CHECK_FALSE(probe.sphere_separate);
}

TEST_CASE("dsl-where-clause: spheres_overlap is usable as an ordinary pure where: predicate",
          "[runtime][codegen-entt][where-clause][stdlib-collision]") {
    // BodyNear1/BodyNear2 overlap each other (distance 1.0 < radius sum 2.0);
    // BodyFar is separated from both. `where: a != b` and
    // `where: vol.spheres_overlap(...)` on the same DetectOverlapWhere rule
    // conjoin: self-pairs and non-overlapping pairs never begin their
    // handler body invocation, so only the two overlapping directed tuples
    // ((Near1,Near2) and (Near2,Near1)) emit OverlapDetected.
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    drive_frame(registry);

    const auto view = registry.view<collision_predicates_runtime__Body>();
    REQUIRE(view.size() == 3);
    for (auto entity : view) {
        const auto& body = registry.get<collision_predicates_runtime__Body>(entity);
        if (body.position.x > 5.0F) {
            CHECK(body.overlap_hits == 0);  // BodyFar
        } else {
            CHECK(body.overlap_hits == 1);  // BodyNear1 / BodyNear2
        }
    }
}

TEST_CASE("stdlib-collision: sphere_box_separation handles contact, penetration, inside ties, and rotation",
          "[runtime][codegen-entt][stdlib-collision]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    drive_frame(registry);

    const auto view = registry.view<collision_predicates_runtime__Probe>();
    REQUIRE(view.size() == 1);
    const auto& probe = registry.get<collision_predicates_runtime__Probe>(*view.begin());

    CHECK(probe.sphere_box_separate.x == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_separate.y == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_separate.z == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_touch.x == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_touch.y == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_touch.z == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_overlap.x == Catch::Approx(0.5F));
    CHECK(probe.sphere_box_overlap.y == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_overlap.z == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_inside_tie.x == Catch::Approx(1.5F));
    CHECK(probe.sphere_box_inside_tie.y == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_inside_tie.z == Catch::Approx(0.0F));
    CHECK(probe.sphere_box_rotated.x == Catch::Approx(0.0F).margin(0.0001F));
    CHECK(probe.sphere_box_rotated.y == Catch::Approx(0.5F).margin(0.0001F));
    CHECK(probe.sphere_box_rotated.z == Catch::Approx(0.0F).margin(0.0001F));
}

TEST_CASE("stdlib-collision: sphere_box_separation remains usable in a pure where predicate",
          "[runtime][codegen-entt][where-clause][stdlib-collision]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    drive_frame(registry);

    const auto view = registry.view<collision_predicates_runtime__SphereBoxCandidate>();
    REQUIRE(view.size() == 1);
    CHECK(registry.get<collision_predicates_runtime__SphereBoxCandidate>(*view.begin()).accepted);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
