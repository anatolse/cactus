// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"
#include "linearization_check.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

namespace game    = cactus_gen::clamped_fields;
namespace backend = cactus::runtime::entt_backend;

struct World {
    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().clamped_fields;
    }

    entt::registry registry;

    World() {
        backend::clear_runtime_faults();
        cactus_headless_test::init_and_load(registry);
    }

    void frames(int count) {
        for (int frame = 0; frame < count; ++frame) {
            cactus_headless_test::drive_frame(registry);
        }
    }

    template <typename Trait>
    [[nodiscard]] Trait& probe() {
        return registry.get<Trait>(slots().Probe);
    }
};

// Gauge.value after one frame, with GaugeUp and GaugeDown run in the given order.
int gauge_after_one_frame(bool swapped) {
    backend::generated_reset_scheduler_state();
    backend::set_linearization_swap(
        backend::LinearizationPair{.first  = "clamped_fields.GaugeUp/on std.core.tick",
                                   .second = "clamped_fields.GaugeDown/on std.core.tick"},
        swapped);
    int value = 0;
    {
        World world;
        world.frames(1);
        value = world.probe<game::Gauge>().value;
    }
    backend::set_linearization_swap(std::nullopt);
    backend::generated_reset_scheduler_state();
    return value;
}

}  // namespace

TEST_CASE("an assignment above the bound is moved to it", "[runtime][clamped-fields]") {
    World world;
    world.probe<game::Feed>().source = 1.5F;
    world.frames(1);
    CHECK(world.probe<game::Feed>().level == 1.0F);
    world.probe<game::Feed>().source = -2.0F;
    world.frames(1);
    CHECK(world.probe<game::Feed>().level == 0.0F);
}

TEST_CASE("a set above the bound is moved to it", "[runtime][clamped-fields]") {
    World world;
    world.probe<game::Dial>().request = 50;
    world.frames(1);
    CHECK(world.probe<game::Dial>().level == 10);
    world.probe<game::Dial>().request = -3;
    world.frames(1);
    CHECK(world.probe<game::Dial>().level == 0);
}

TEST_CASE("a NaN assignment becomes the minimum", "[runtime][clamped-fields]") {
    World world;
    world.probe<game::Feed>().source = std::numeric_limits<float>::quiet_NaN();
    world.frames(1);
    CHECK(world.probe<game::Feed>().level == 0.0F);
}

TEST_CASE("unordered deltas at the bound give the bound in both orders", "[runtime][clamped-fields]") {
    CHECK(gauge_after_one_frame(false) == 100);
    CHECK(gauge_after_one_frame(true) == 100);
    CHECK(cactus::test::require_pair_swaps_match(3, cactus::test::no_setup()) > 0);
}

TEST_CASE("a reader between two writers reads the bound and changes nothing", "[runtime][clamped-fields]") {
    World world;
    world.frames(1);
    CHECK(world.probe<game::Health>().seen == 100.0F);
    CHECK(world.probe<game::Health>().hp == 100.0F);
    CHECK(world.probe<game::Twin>().hp == 100.0F);
    world.frames(2);
    CHECK(world.probe<game::Health>().hp == 100.0F);
}

TEST_CASE("the next activation starts from the projected value", "[runtime][clamped-fields]") {
    World world;
    world.frames(1);
    CHECK(world.probe<game::Carry>().value == 100.0F);
    world.frames(1);
    CHECK(world.probe<game::Carry>().value == 50.0F);
}

TEST_CASE("a one-sided int at the type's maximum does not wrap", "[runtime][clamped-fields]") {
    World world;
    world.frames(2);
    CHECK(world.probe<game::Counter>().count == std::numeric_limits<std::int32_t>::max());
}

TEST_CASE("a duration countdown stops at zero", "[runtime][clamped-fields]") {
    World world;
    world.frames(1);
    CHECK(world.probe<game::Fatigue>().left == cactus::Duration{});
}

TEST_CASE("creation projects the value and faults on crossed let bounds", "[runtime][clamped-fields]") {
    World world;
    const auto view = world.registry.view<game::Band>();
    REQUIRE(view.size() == 1);
    const auto& band = world.registry.get<game::Band>(view.front());
    CHECK(band.lo == 2);
    CHECK(band.value == 2);
    const auto& faults = backend::runtime_faults();
    CHECK(std::ranges::any_of(faults, [](const backend::RangeFault& fault) {
        return fault.trait == "Band" && fault.field == "value" &&
               fault.reason == backend::RangeFaultReason::CrossedBounds;
    }));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
