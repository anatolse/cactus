// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("std.grid turns a coordinate into a cell and a cell into its centre", "[runtime][stdlib][trait-dimensions]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);
    const auto& samples =
        registry.get<cactus_gen::grid::Samples>(cactus_gen::detail::generated_named_slots().grid.Probe);
    CHECK(samples.inside == 2);
    CHECK(samples.negative == -1);
    CHECK(samples.edge == 4);
    CHECK(samples.center == 1.25F);
    CHECK(samples.back == -7);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
