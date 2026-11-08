// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>

namespace {

namespace game = cactus_gen::singleton_traits_immediate;
namespace handlers = cactus_gen::detail::singleton_traits_immediate;
using Reason = cactus::runtime::entt_backend::RangeFaultReason;

std::size_t second_carrier_faults() {
    const auto& faults = cactus::runtime::entt_backend::runtime_faults();
    return static_cast<std::size_t>(std::ranges::count_if(faults, [](const auto& fault) {
        return fault.reason == Reason::SecondCarrier && fault.trait == "Terrain";
    }));
}

}  // namespace

// A program without phases applies each spawn and add at once, outside any
// commit round, so the live registry is what refuses a second carrier.
TEST_CASE("without phases a single trait still refuses a second carrier", "[runtime][single-traits]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    const auto& slots = cactus_gen::detail::generated_named_slots().singleton_traits_immediate;
    REQUIRE(registry.view<game::Terrain>().size() == 1);

    handlers::make_on(registry, game::pokeEvent{});
    CHECK(registry.view<game::Terrain>().size() == 1);
    CHECK(second_carrier_faults() == 1);

    handlers::give_on(registry, game::giveEvent{});
    CHECK_FALSE(registry.all_of<game::Terrain>(slots.Other));
    CHECK(second_carrier_faults() == 2);

    registry.destroy(slots.First);
    handlers::make_on(registry, game::pokeEvent{});
    REQUIRE(registry.view<game::Terrain>().size() == 1);
    const auto spawned = *registry.view<game::Terrain>().begin();
    CHECK(second_carrier_faults() == 2);

    registry.remove<game::Terrain>(spawned);
    handlers::give_on(registry, game::giveEvent{});
    CHECK(registry.get<game::Terrain>(slots.Other).size == 5);
    CHECK(second_carrier_faults() == 2);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
