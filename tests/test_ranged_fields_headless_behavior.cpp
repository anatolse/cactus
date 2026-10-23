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

using Arrived = cactus_gen::ranged_fields::Arrived;
using Cell    = cactus_gen::ranged_fields::Cell;
using Level   = cactus_gen::ranged_fields::Level;
using Log     = cactus_gen::ranged_fields::Log;
using Probe   = cactus_gen::ranged_fields::Probe;
using Row     = cactus_gen::ranged_fields::Row;
using Reason  = cactus::runtime::entt_backend::RangeFaultReason;

std::size_t faults_with(Reason reason) {
    const auto& faults = cactus::runtime::entt_backend::runtime_faults();
    return static_cast<std::size_t>(
        std::ranges::count(faults, reason, &cactus::runtime::entt_backend::RangeFault::reason));
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("ranged let fields refuse values made outside their range", "[runtime][ranged-fields]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots = cactus_gen::detail::generated_named_slots().ranged_fields;
    const auto& log   = registry.get<Log>(slots.Recorder);

    {  // in-range values behave as without ranges
        REQUIRE(registry.valid(slots.Placed));
        CHECK(registry.get<Cell>(slots.Placed).x == 7);
        REQUIRE(registry.valid(log.good));
        CHECK(registry.get<Cell>(log.good).x == 3);
        CHECK(registry.get<Cell>(log.good).volume == slots.Grid);
    }

    {  // a declared entity out of range is not created and its handle is stale
        CHECK_FALSE(registry.valid(slots.Stray));
    }

    {  // an out-of-range spawn creates nothing, so its handle is stale and no trigger fires
        CHECK_FALSE(registry.valid(log.too_far));
        CHECK_FALSE(registry.valid(log.below));
        CHECK(registry.view<Cell>().size() == 4);
        CHECK(registry.view<Arrived>().size() == 4);
    }

    {  // a child out of range stops the whole spawn, root and children
        REQUIRE(registry.valid(log.good_row));
        CHECK_FALSE(registry.valid(log.bad_row));
        CHECK(registry.view<Row>().size() == 1);
    }

    {  // a spawn against a stale container or a non-container creates nothing
        CHECK_FALSE(registry.valid(log.orphan));
        CHECK_FALSE(registry.valid(log.wrong_container));
    }

    {  // a container spawned earlier in command order is visible to the check
        REQUIRE(registry.valid(log.fresh_cell));
        CHECK(registry.get<Cell>(log.fresh_cell).volume == log.fresh_volume);
        CHECK_FALSE(registry.valid(log.fresh_too_far));
    }

    {  // an out-of-range add leaves the entity as it was
        CHECK(registry.get<Level>(slots.Kept).value == 1);
        CHECK(registry.get<Level>(slots.Raised).value == 2);
    }

    {  // each failure is reported with the trait, field, value and range
        const auto& faults = cactus::runtime::entt_backend::runtime_faults();
        CHECK(faults.size() == 8);
        CHECK(faults_with(Reason::OutOfRange) == 6);
        CHECK(faults_with(Reason::StaleContainer) == 1);
        CHECK(faults_with(Reason::MissingContainerTrait) == 1);
        const auto too_far = std::ranges::find_if(faults, [](const auto& fault) {
            return fault.trait == "Cell" && fault.value == 8 && fault.reason == Reason::OutOfRange;
        });
        REQUIRE(too_far != faults.end());
        CHECK(too_far->field == "x");
        CHECK(too_far->lo == 0);
        CHECK(too_far->hi == 8);
    }

    cactus::runtime::entt_backend::clear_runtime_faults();
    cactus_headless_test::drive_frame(registry);

    {  // an out-of-range project leaves the trait unprojected
        CHECK(registry.get<Probe>(slots.Kept).glow == -1);
        CHECK(registry.get<Probe>(slots.Raised).glow == 2);
        CHECK(faults_with(Reason::OutOfRange) == 1);
    }

    {  // destroying a container keeps the cells ranged against it
        CHECK_FALSE(registry.valid(log.fresh_volume));
        REQUIRE(registry.valid(log.fresh_cell));
        CHECK(registry.get<Cell>(log.fresh_cell).x == 1);
    }

    cactus_headless_test::drive_frame(registry);

    {  // a spawn against a destroyed container creates nothing
        CHECK_FALSE(registry.valid(log.after_destroy));
        CHECK(faults_with(Reason::StaleContainer) == 1);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
