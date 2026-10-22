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

using Pool  = cactus_gen::accumulation_per_cell::Pool;
using Seen  = cactus_gen::accumulation_per_cell::Seen;
using Gauge = cactus_gen::accumulation_per_cell::Gauge;
using Probe = cactus_gen::accumulation_per_cell::Probe;

struct OneFrame {
    entt::registry registry;

    OneFrame() {
        cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    }

    template <typename Trait>
    const Trait& of(entt::entity entity) {
        REQUIRE(registry.valid(entity));
        return registry.get<Trait>(entity);
    }
};

}  // namespace

TEST_CASE("a read of another entity does not end the interval", "[runtime][codegen-entt][write-kinds]") {
    OneFrame world;
    const auto first  = cactus_gen::detail::generated_named_slots().accumulation_per_cell.First;
    const auto second = cactus_gen::detail::generated_named_slots().accumulation_per_cell.Second;
    CHECK(world.of<Seen>(first).level == 1.0e8F);
    CHECK(world.of<Pool>(first).level == 0.0F);
    CHECK(world.of<Pool>(second).level == 1.0F);
}

TEST_CASE("a guarded read that does not run does not end the interval", "[runtime][codegen-entt][write-kinds]") {
    OneFrame world;
    const auto meter = cactus_gen::detail::generated_named_slots().accumulation_per_cell.Meter;
    CHECK(world.of<Probe>(meter).level == -1.0F);
    CHECK(world.of<Gauge>(meter).level == 1.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
