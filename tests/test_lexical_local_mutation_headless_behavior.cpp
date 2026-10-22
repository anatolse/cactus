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
    cactus::runtime::entt_backend::generated_inject_external_event(
        cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

}  // namespace

TEST_CASE("nested lexical locals mutate outer accumulators and cursors at runtime",
          "[runtime][codegen-entt][lexical-locals]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto results_view = registry.view<cactus_gen::lexical_local_mutation_runtime::Results>();
    REQUIRE(results_view.begin() != results_view.end());
    const auto observer = *results_view.begin();

    const auto boss = registry.create();
    registry.emplace<cactus_gen::lexical_local_mutation_runtime::Boss>(
        boss, cactus_gen::lexical_local_mutation_runtime::Boss{.rank = 7});
    registry.get<cactus_gen::lexical_local_mutation_runtime::Results>(observer).subject = boss;

    drive_frame(registry);

    const auto& results = registry.get<cactus_gen::lexical_local_mutation_runtime::Results>(observer);
    CHECK(results.sum == 17);
    CHECK(results.maximum == 7);
    CHECK(results.count == 4);
    CHECK(results.cursor == 5);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)