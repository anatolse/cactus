// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>

namespace {

using Arrived = cactus_gen::singleton_traits::Arrived;
using Log     = cactus_gen::singleton_traits::Log;
using Seed    = cactus_gen::singleton_traits::Seed;
using Terrain = cactus_gen::singleton_traits::Terrain;
using Reason  = cactus::runtime::entt_backend::RangeFaultReason;

std::size_t second_carrier_faults() {
    const auto& faults = cactus::runtime::entt_backend::runtime_faults();
    return static_cast<std::size_t>(std::ranges::count_if(faults, [](const auto& fault) {
        return fault.reason == Reason::SecondCarrier && fault.trait == "Terrain";
    }));
}

std::size_t carriers(entt::registry& registry) {
    return registry.view<Terrain>().size();
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("a single trait refuses a second carrier", "[runtime][single-traits]") {
    entt::registry registry;
    cactus::runtime::entt_backend::clear_runtime_faults();
    cactus_headless_test::init_and_load(registry);
    const auto& slots = cactus_gen::detail::generated_named_slots().singleton_traits;
    const auto& log   = registry.get<Log>(slots.Recorder);

    REQUIRE(registry.all_of<Terrain, Arrived>(slots.First));
    REQUIRE(carriers(registry) == 1);

    cactus_headless_test::drive_frame(registry);
    {  // a second spawn creates nothing, so its handle is stale
        CHECK_FALSE(registry.valid(log.second));
        CHECK(carriers(registry) == 1);
        CHECK(second_carrier_faults() == 1);
        CHECK(cactus::runtime::entt_backend::describe_range_fault(cactus::runtime::entt_backend::runtime_faults().back())
                  .contains("Terrain"));
    }

    cactus_headless_test::drive_frame(registry);
    {  // an add to a second entity does nothing and fires no trigger
        CHECK_FALSE(registry.any_of<Terrain, Arrived>(slots.Other));
        CHECK(second_carrier_faults() == 2);
    }

    cactus_headless_test::drive_frame(registry);
    {  // re-adding to the carrier replaces its fields
        CHECK(registry.get<Terrain>(slots.First).size == 7);
        CHECK(second_carrier_faults() == 2);
    }

    cactus_headless_test::drive_frame(registry);
    {  // a spawn before the carrier's destroy in the same round succeeds
        CHECK_FALSE(registry.valid(slots.First));
        REQUIRE(registry.valid(log.replaced));
        CHECK(registry.all_of<Terrain, Arrived>(log.replaced));
        CHECK(second_carrier_faults() == 2);
    }

    cactus_headless_test::drive_frame(registry);
    {  // a spawn after the carrier's destroy in the same round succeeds
        CHECK_FALSE(registry.valid(log.replaced));
        REQUIRE(registry.valid(log.moved_to));
        CHECK(registry.all_of<Terrain>(log.moved_to));
        CHECK(second_carrier_faults() == 2);
    }

    cactus_headless_test::drive_frame(registry);
    {  // an add before the carrier's remove in the same round moves the trait
        CHECK(registry.get<Terrain>(slots.Other).size == 6);
        CHECK(registry.all_of<Arrived>(slots.Other));
        CHECK_FALSE(registry.all_of<Terrain>(log.moved_to));
        CHECK(second_carrier_faults() == 2);
    }

    cactus_headless_test::drive_frame(registry);
    {  // an add after the carrier's remove in the same round moves the trait back
        CHECK_FALSE(registry.all_of<Terrain>(slots.Other));
        CHECK(registry.get<Terrain>(log.moved_to).size == 8);
        CHECK(second_carrier_faults() == 2);
    }

    cactus_headless_test::drive_frame(registry);
    {  // a spawned child can be the carrier
        CHECK_FALSE(registry.valid(log.moved_to));
        REQUIRE(registry.valid(log.holder));
        CHECK_FALSE(registry.all_of<Terrain>(log.holder));
        CHECK(carriers(registry) == 1);
        CHECK(second_carrier_faults() == 2);
    }

    cactus_headless_test::drive_frame(registry);
    {  // destroying the carrier's owner frees the trait in the same round
        CHECK_FALSE(registry.valid(log.holder));
        REQUIRE(registry.valid(log.after_owner));
        CHECK(registry.all_of<Terrain>(log.after_owner));
        CHECK(carriers(registry) == 1);
        CHECK(second_carrier_faults() == 2);
    }

    cactus_headless_test::drive_frame(registry);
    REQUIRE(carriers(registry) == 0);

    cactus_headless_test::drive_frame(registry);
    {  // rows of one pass spawn in creation order; only the first succeeds
        const auto& first = registry.get<Seed>(slots.SeedA).made;
        REQUIRE(registry.valid(first));
        CHECK(registry.all_of<Terrain>(first));
        CHECK_FALSE(registry.valid(registry.get<Seed>(slots.SeedB).made));
        CHECK_FALSE(registry.valid(registry.get<Seed>(slots.SeedC).made));
        CHECK(carriers(registry) == 1);
        CHECK(second_carrier_faults() == 4);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
