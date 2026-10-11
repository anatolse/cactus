// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "linearization_check.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("the harness sees an order that only declaration order fixed",
          "[runtime][codegen-entt][handler-execution-graph]") {
    CHECK(cactus::test::some_linearization_differs(2, cactus::test::no_setup(), true));
}

TEST_CASE("keeping declaration-order edges, every seed matches the stable order",
          "[runtime][codegen-entt][handler-execution-graph]") {
    cactus::test::require_linearizations_match(2, cactus::test::no_setup());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
