// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

// Built twice: from examples/native_field_reads.cactus and from a copy that
// declares Look before Move. Both must give this value.

TEST_CASE("a native reader sees the writer's value in either declaration order",
          "[runtime][codegen-entt][native-field-reads]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    auto view = registry.view<cactus_gen::native_field_reads::Seen>();
    REQUIRE(view.size() == 1);
    CHECK(registry.get<cactus_gen::native_field_reads::Seen>(*view.begin()).x == 1.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
