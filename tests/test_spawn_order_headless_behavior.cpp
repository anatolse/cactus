// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ordered spawners give an order-observing rule the same result on every run",
          "[runtime][codegen-entt][spawn-order]") {
    for (int run = 0; run < 5; ++run) {
        entt::registry registry;
        cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
        const auto view = registry.view<cactus_gen::spawn_order_runtime::Winner>();
        REQUIRE(view.size() == 1);
        CHECK(registry.get<cactus_gen::spawn_order_runtime::Winner>(view.front()).kind == 1);
        CHECK(registry.view<cactus_gen::spawn_order_runtime::Pickup>().size() == 2);
    }
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
