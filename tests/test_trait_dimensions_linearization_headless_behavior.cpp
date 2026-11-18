// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "linearization_check.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("unordered writers of cells give the same world in every order", "[runtime][trait-dimensions]") {
    cactus::test::require_linearizations_match(4, cactus::test::no_setup());
}

TEST_CASE("the two += writers of one cell commute when swapped", "[runtime][trait-dimensions]") {
    namespace backend = cactus::runtime::entt_backend;
    CHECK(cactus::test::require_pair_swaps_match(4, cactus::test::no_setup()) >= 1);
    const backend::LinearizationPair writers{.first  = "trait_dimensions.Blow/on std.core.tick",
                                             .second = "trait_dimensions.Throw/on std.core.tick"};
    const backend::LinearizationPair swapped{.first = writers.second, .second = writers.first};
    const auto& pairs = backend::linearization_pairs();
    CHECK((std::ranges::contains(pairs, writers) || std::ranges::contains(pairs, swapped)));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
