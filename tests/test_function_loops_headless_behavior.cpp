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

const cactus_gen::function_loops_runtime::Results& computed_results(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    drive_frame(registry);
    const auto view = registry.view<cactus_gen::function_loops_runtime::Results>();
    REQUIRE(view.begin() != view.end());
    return registry.get<cactus_gen::function_loops_runtime::Results>(*view.begin());
}

}  // namespace

TEST_CASE("pure function loops reduce and search over finite values", "[runtime][codegen-entt][function-loops]") {
    entt::registry registry;
    const auto& results = computed_results(registry);

    CHECK(results.total == 7.0F);
    CHECK(results.found_at == 2);
    CHECK(results.missing_at == -1);
    CHECK(results.nearest_pair == 1);
}

TEST_CASE("a return inside nested function loops exits the whole function", "[runtime][codegen-entt][function-loops]") {
    entt::registry registry;
    CHECK(computed_results(registry).cell == 102);
}

TEST_CASE("function ranges are finite: empty, zero-step, wrong direction and captured bounds",
          "[runtime][codegen-entt][function-loops]") {
    entt::registry registry;
    const auto& results = computed_results(registry);

    CHECK(results.empty_count == 0);
    CHECK(results.zero_step_count == 0);
    CHECK(results.wrong_direction_count == 0);
    CHECK(results.captured_count == 3);
    CHECK(results.countdown_sum == 22);
}

TEST_CASE("ranges in functions and handlers stop at the integer limits without overflowing",
          "[runtime][codegen-entt][function-loops]") {
    entt::registry registry;
    const auto& results = computed_results(registry);

    CHECK(results.top_count == 2);
    CHECK(results.bottom_count == 2);
    CHECK(results.handler_top_count == 2);
}

TEST_CASE("function loops change only local copies of arguments", "[runtime][codegen-entt][function-loops]") {
    entt::registry registry;
    const auto& results = computed_results(registry);

    CHECK(results.bumped == 10);
    CHECK(results.argument == 7);
}

TEST_CASE("an imported function's loop runs like a local one", "[runtime][codegen-entt][function-loops]") {
    entt::registry registry;
    CHECK(computed_results(registry).imported_sum == 10);
}

TEST_CASE("a constant may be computed by a function loop", "[runtime][codegen-entt][function-loops]") {
    entt::registry registry;
    CHECK(computed_results(registry).constant_count == 4);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
