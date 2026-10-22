// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

void touch_world();

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using Pool = cactus_gen::accumulation_storage_move::Pool;

entt::registry* world = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

// Destroying Spare swaps Tank's Pool into Spare's slot.
void touch_world() {
    world->destroy(cactus_gen::detail::generated_named_slots().accumulation_storage_move.Spare);
}

TEST_CASE("a storage-moving handler does not end the interval", "[runtime][codegen-entt][write-kinds]") {
    entt::registry registry;
    world = &registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    world = nullptr;

    REQUIRE(registry.view<Pool>().size() == 1);
    const auto tank = cactus_gen::detail::generated_named_slots().accumulation_storage_move.Tank;
    REQUIRE(registry.valid(tank));
    // One merged interval gives 1.0; rounding 1.0e8 + 1.0 first loses the 1.0.
    CHECK(registry.get<Pool>(tank).level == 1.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
