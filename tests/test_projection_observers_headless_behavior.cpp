// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("observers of a projected trait run after its projector in either declaration order",
          "[runtime][codegen-entt][projected-traits]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto target = cactus_gen::detail::generated_named_slots().projection_observers.Target;
    REQUIRE(registry.valid(target));
    const auto& body = registry.get<cactus_gen::projection_observers::Body>(target);
    CHECK(body.unmarked == 0);
    CHECK(body.found == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
