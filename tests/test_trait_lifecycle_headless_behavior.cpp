// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using Log     = trait_lifecycle__Log;
using Tally   = trait_lifecycle__Tally;
using Dying   = trait_lifecycle__Dying;
using Fading  = trait_lifecycle__Fading;
using Burning = trait_lifecycle__Burning;

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("trait lifecycle triggers react to arrival and departure of traits", "[runtime][trait-lifecycle]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots   = generated_named_slots();
    const auto grunt    = slots.trait_lifecycle__Grunt;
    const auto scout    = slots.trait_lifecycle__Scout;
    const auto deserter = slots.trait_lifecycle__Deserter;
    const auto torch    = slots.trait_lifecycle__Torch;
    const auto wreck    = slots.trait_lifecycle__Wreck;
    const auto stats    = slots.trait_lifecycle__Stats;

    {  // a placed entity fires on added before on load
        CHECK(registry.get<Log>(grunt).arrived);
        CHECK(registry.get<Log>(grunt).saw_arrival);
        CHECK(registry.get<Log>(scout).saw_arrival);
        CHECK_FALSE(registry.get<Log>(torch).arrived);
    }

    {  // on added runs only for the entity that gained the trait
        CHECK(registry.get<Log>(grunt).dying_count == 1);
        CHECK(registry.get<Log>(scout).dying_count == 0);
        CHECK(registry.get<Dying>(grunt).elapsed == 1.5F);
    }

    {  // the filter is checked in the committed state: Deserter lost Enemy in the same round
        CHECK(registry.all_of<Dying>(deserter));
        CHECK(registry.get<Log>(deserter).dying_count == 0);
        CHECK_FALSE(registry.get<Log>(deserter).faded);
    }

    {  // chained adds commit in a later round of the same activation
        CHECK(registry.all_of<Fading>(grunt));
        CHECK(registry.get<Log>(grunt).faded);
        CHECK_FALSE(registry.get<Log>(scout).faded);
    }

    {  // on removed binds the value from before removal
        CHECK_FALSE(registry.all_of<Burning>(torch));
        CHECK(registry.get<Log>(torch).last_intensity == 3.0F);
    }

    {  // destroying an entity fires nothing
        CHECK_FALSE(registry.valid(wreck));
        CHECK(registry.get<Tally>(stats).removed_burning == 1);
    }

    {  // a later frame fires nothing without a trait change
        cactus_headless_test::drive_frame(registry);
        CHECK(registry.get<Log>(grunt).dying_count == 1);
        CHECK(registry.get<Tally>(stats).removed_burning == 1);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
