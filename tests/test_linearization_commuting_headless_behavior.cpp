// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "linearization_check.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("unordered accumulations give the same world in every order",
          "[runtime][codegen-entt][handler-execution-graph][write-kinds]") {
    cactus::test::require_linearizations_match(3, cactus::test::no_setup());
}
TEST_CASE("the two unordered accumulations are listed as a pair and commute when swapped",
          "[runtime][codegen-entt][handler-execution-graph][write-kinds]") {
    namespace backend = cactus::runtime::entt_backend;
    CHECK(cactus::test::require_pair_swaps_match(3, cactus::test::no_setup()) == 1);
    REQUIRE(backend::linearization_pairs().size() == 1);
    CHECK(backend::linearization_pairs().front() ==
          backend::LinearizationPair{.first  = "linearization_commuting_runtime.First/on std.core.tick",
                                     .second = "linearization_commuting_runtime.Second/on std.core.tick"});
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
