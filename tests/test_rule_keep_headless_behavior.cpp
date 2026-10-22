// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

using Walker  = cactus_gen::rule_keep::Walker;
using InLava  = cactus_gen::rule_keep::InLava;
using Lava    = cactus_gen::rule_keep::Lava;
using Ranked  = cactus_gen::rule_keep::Ranked;
using Tally   = cactus_gen::rule_keep::Tally;
using Control = cactus_gen::rule_keep::Control;

constexpr float kDt = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    // Runs `count` fixed ticks, one per 1/60 s frame.
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            const int before = ticks();
            cactus_headless_test::drive_frame(registry, kDt);
            REQUIRE(ticks() == before + 1);
        }
    }
    int ticks() {
        return registry.get<Tally>(cactus_gen::detail::generated_named_slots().rule_keep.Clock).ticks;
    }
    int total_exits() {
        return registry.get<Tally>(cactus_gen::detail::generated_named_slots().rule_keep.Clock).exits;
    }
    Walker& walker(entt::entity entity) {
        return registry.get<Walker>(entity);
    }
    bool in_lava(entt::entity entity) {
        return registry.all_of<InLava>(entity);
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("keep adds, patches and removes a trait as its group gains and loses rows", "[runtime][rule-keep]") {
    World world;
    auto& registry    = world.registry;
    const auto& slots = cactus_gen::detail::generated_named_slots();
    const auto hiker  = slots.rule_keep.Hiker;
    const auto camper = slots.rule_keep.Camper;
    const auto loner  = slots.rule_keep.Loner;
    const auto judge  = slots.rule_keep.Judge;

    {  // nothing is kept before the first run
        CHECK_FALSE(world.in_lava(hiker));
    }

    {  // entering fires on added once and writes this run's values
        world.tick();
        REQUIRE(world.in_lava(hiker));
        CHECK(registry.get<InLava>(hiker).zones == 1);
        CHECK(registry.get<InLava>(hiker).heat == 2.0F);
        CHECK(registry.get<InLava>(hiker).hottest == slots.rule_keep.PoolA);
        CHECK(world.walker(hiker).enters == 1);
        CHECK_FALSE(world.in_lava(slots.rule_keep.Loner));
        world.tick(2);
        CHECK(world.walker(hiker).enters == 1);
        CHECK(world.walker(hiker).exits == 0);
    }

    {  // best: the highest key wins, the earlier row wins a tie, an empty group is stale
        CHECK(registry.get<Ranked>(hiker).top == slots.rule_keep.PoolA);
        CHECK(registry.get<Ranked>(judge).top == slots.rule_keep.PoolD);
        CHECK(registry.get<Ranked>(judge).hot == slots.rule_keep.PoolD);
        CHECK_FALSE(registry.valid(registry.get<Ranked>(loner).top));
    }

    {  // best skips a NaN key
        registry.get<Lava>(slots.rule_keep.PoolD).heat = std::numeric_limits<float>::quiet_NaN();
        world.tick();
        CHECK(registry.get<Ranked>(judge).hot == slots.rule_keep.PoolE);
    }

    {  // overlapping zones patch the fields and fire nothing
        world.walker(hiker).x = 0.75F;
        world.tick();
        CHECK(registry.get<InLava>(hiker).zones == 2);
        CHECK(registry.get<InLava>(hiker).heat == 5.0F);
        CHECK(registry.get<InLava>(hiker).hottest == slots.rule_keep.PoolB);
        world.walker(hiker).x = 2.0F;
        world.tick();
        CHECK(registry.get<InLava>(hiker).zones == 1);
        CHECK(world.walker(hiker).enters == 1);
        CHECK(world.walker(hiker).exits == 0);
    }

    {  // leaving the last zone fires on removed with the last kept value
        world.walker(hiker).x = 5.0F;
        world.tick();
        CHECK_FALSE(world.in_lava(hiker));
        CHECK(world.walker(hiker).exits == 1);
        CHECK(world.walker(hiker).last_zones == 1);
    }

    {  // the only zone is destroyed
        REQUIRE(world.in_lava(camper));
        registry.destroy(slots.rule_keep.PoolC);
        world.tick();
        CHECK_FALSE(world.in_lava(camper));
        CHECK(world.walker(camper).exits == 1);
    }

    {  // the kept entity leaves the per binding
        const auto deserter = slots.rule_keep.Deserter;
        REQUIRE(world.in_lava(deserter));
        registry.remove<Walker>(deserter);
        world.tick();
        CHECK_FALSE(world.in_lava(deserter));
    }

    {  // a destroyed kept entity fires nothing
        const auto doomed = slots.rule_keep.Doomed;
        REQUIRE(world.in_lava(doomed));
        const int exits = world.total_exits();
        registry.destroy(doomed);
        world.tick();
        CHECK(world.total_exits() == exits);
    }

    {  // when: false freezes presence until the rule runs again
        world.walker(hiker).x = 0.0F;
        world.tick();
        REQUIRE(world.in_lava(hiker));
        CHECK(world.walker(hiker).enters == 2);
        registry.get<Control>(slots.rule_keep.Settings).frozen = true;
        world.walker(hiker).x = 5.0F;
        world.tick();
        CHECK(world.in_lava(hiker));
        registry.get<Control>(slots.rule_keep.Settings).frozen = false;
        world.tick();
        CHECK_FALSE(world.in_lava(hiker));
        CHECK(world.walker(hiker).exits == 2);
    }

    {  // a frame with no fixed step changes nothing
        world.walker(hiker).x = 0.0F;
        world.tick();
        REQUIRE(world.in_lava(hiker));
        world.walker(hiker).x = 5.0F;
        const int ticks = world.ticks();
        cactus_headless_test::drive_frame(registry, 0.0F);
        CHECK(world.ticks() == ticks);
        CHECK(world.in_lava(hiker));
        world.tick();
        CHECK_FALSE(world.in_lava(hiker));
    }

    {  // restored state is reconciled by the next run
        world.walker(hiker).x = 0.0F;
        world.tick();
        REQUIRE(world.in_lava(hiker));
        const int enters = world.walker(hiker).enters;
        registry.remove<InLava>(hiker);
        registry.emplace<InLava>(loner);
        world.tick();
        CHECK(world.in_lava(hiker));
        CHECK(world.walker(hiker).enters == enters + 1);
        CHECK_FALSE(world.in_lava(loner));
        CHECK(world.walker(loner).exits == 1);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
