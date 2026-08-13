// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

}  // namespace

TEST_CASE("pair handler return ends only the current tuple, not the remaining pairs in the pass",
          "[runtime][codegen-entt][pair-relations]") {
    // Regression test for the DetectBubbleContact-style bug (examples/bouncy-bubbles):
    // a `pairs:` handler that rejects self-pairs with `if a == b: return` must not let
    // that `return` abort the whole tuple pass. Left-binding-major iteration over
    // identical `a`/`b` snapshots visits the self-pair (Ball1, Ball1) first — if
    // `return` escaped the per-tuple invocation instead of ending only that tuple,
    // every ball would end this frame with hits == 0.
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    drive_frame(registry);

    const auto view = registry.view<pair_handler_return_runtime__Ball>();
    std::size_t ball_count = 0;
    for (auto entity : view) {
        ++ball_count;
        // Each ball is `a` in exactly 2 of the 6 non-self directed tuples
        // among 3 balls.
        CHECK(registry.get<pair_handler_return_runtime__Ball>(entity).hits == 2);
    }
    REQUIRE(ball_count == 3);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
