// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "linearization_check.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("every linear extension of the fixture gives the stable order's world", "[linearization-survey]") {
    cactus::test::require_linearizations_match(30, cactus::test::no_setup());
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
