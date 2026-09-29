// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

using Reading = event_payload_reads__Reading;

TEST_CASE("handlers read quat and int payload fields through the bare or qualified trigger name",
          "[runtime][events][trigger-name]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& reading = registry.get<Reading>(generated_named_slots().event_payload_reads__Probe);

    const auto expected = cactus::runtime::stdlib::math::quat::from_euler(0.0F, 1.0F, 0.0F);
    CHECK(reading.rotation.x == expected.x);
    CHECK(reading.rotation.y == expected.y);
    CHECK(reading.rotation.z == expected.z);
    CHECK(reading.rotation.w == expected.w);
    CHECK(reading.amount == 3);
    CHECK(reading.echoed == 3);
    CHECK(reading.hit == 5);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
