// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace game = cactus_gen::projection_rows;

TEST_CASE("a projection does not drop a later row of the pass that observes it",
          "[runtime][codegen-entt][projected-traits]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto& slots = cactus_gen::detail::generated_named_slots().projection_rows;
    CHECK(registry.get<game::Row>(slots.First).runs == 1);
    CHECK(registry.get<game::Row>(slots.Second).runs == 1);
}

TEST_CASE("a pass that observes its own projection runs its rows in creation order",
          "[runtime][codegen-entt][projected-traits]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto& slots = cactus_gen::detail::generated_named_slots().projection_rows;
    CHECK_FALSE(registry.get<game::Probe>(slots.First).saw_mark);
    CHECK(registry.get<game::Probe>(slots.Second).saw_mark);
}

TEST_CASE("a sorted pass that observes its own projection keeps its rows",
          "[runtime][codegen-entt][projected-traits]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto& slots = cactus_gen::detail::generated_named_slots().projection_rows;
    CHECK(registry.get<game::Ranked>(slots.First).runs == 1);
    CHECK(registry.get<game::Ranked>(slots.Second).runs == 1);
}

TEST_CASE("a pass with only a without: filter keeps its rows", "[runtime][codegen-entt][projected-traits]") {
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto& slots  = cactus_gen::detail::generated_named_slots().projection_rows;
    const auto entities = registry.view<cactus::runtime::entt_backend::CreationOrdinal>().size();
    CHECK(registry.get<game::Tally>(slots.Counter).rows == static_cast<int>(entities));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
