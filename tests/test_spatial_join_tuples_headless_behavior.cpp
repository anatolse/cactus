// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

}  // namespace

TEST_CASE("SAP-eligible pair rule visits every tuple (self-pairs included) in left-binding-major, "
         "creation-ordinal order, matching the Cartesian scan",
         "[runtime][codegen-entt][spatial-join]") {
    // DetectOverlap (tests/fixtures/spatial_join_tuples_runtime.cactus) is
    // SAP-eligible: `where:` is a direct call to circles_overlap through a
    // renamed import alias, both bindings require only Body. All three
    // bodies mutually overlap by construction, so every one of the 3x3
    // directed tuples (self-pairs included, no `a != b` filter) survives.
    // RecordTuple's rolling base-10 encoding of (a.tag*3 + b.tag) makes the
    // final sequence a fingerprint of both tuple membership and visitation
    // order -- 12345678 is exactly what the Cartesian double-loop over
    // creation-ordinal-sorted snapshots (0,0)(0,1)(0,2)(1,0)(1,1)(1,2)(2,0)
    // (2,1)(2,2) would also produce, since the recognized predicate is
    // re-verified per tuple regardless of which strategy found the
    // candidate (dsl-pair-relations: "Execution strategy does not affect
    // tuple membership or order").
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    drive_frame(registry);

    const auto view = registry.view<spatial_join_tuples_runtime__Recorder>();
    std::size_t recorder_count = 0;
    for (auto entity : view) {
        ++recorder_count;
        CHECK(registry.get<spatial_join_tuples_runtime__Recorder>(entity).sequence == 12345678);
    }
    REQUIRE(recorder_count == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
