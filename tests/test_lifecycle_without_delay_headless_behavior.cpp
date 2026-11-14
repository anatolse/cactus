// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a lifecycle trigger outside every delivery cycle runs at once beside an event cycle",
          "[runtime][codegen-entt][trait-lifecycle][delivery-cycles]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto& slots = cactus_gen::detail::generated_named_slots().lifecycle_without_delay_runtime;
    using namespace cactus_gen::lifecycle_without_delay_runtime;
    CHECK(registry.get<Door>(slots.Front).knocks == 1);
    CHECK(registry.get<Tally>(slots.Board).marks == 1);
    CHECK_FALSE(registry.all_of<Mark>(slots.Front));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
