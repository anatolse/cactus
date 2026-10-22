// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

using Pool = cactus_gen::accumulation_commit_round::Pool;

TEST_CASE("a commit round ends the accumulation interval", "[runtime][codegen-entt][write-kinds]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    auto view = registry.view<Pool>();
    REQUIRE(view.size() == 1);
    // One merged interval would give 1.0; rounding 1.0e8 + 1.0 first loses the 1.0.
    CHECK(registry.get<Pool>(*view.begin()).level == 0.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
