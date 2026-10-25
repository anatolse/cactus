// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

namespace game = cactus_gen::generalized_keep;

constexpr float kDt = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    [[nodiscard]] static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().generalized_keep;
    }
    game::Log& log() {
        return registry.get<game::Log>(slots().Journal);
    }
    // Runs `count` fixed ticks, one per 1/60 s frame.
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            const int before = log().ticks;
            cactus_headless_test::drive_frame(registry, kDt);
            REQUIRE(log().ticks == before + 1);
        }
    }
    template <typename Trait>
    bool has(entt::entity entity) {
        return registry.all_of<Trait>(entity);
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("generalized keep: unary, unreduced pair and conditional facts", "[runtime][rule-keep]") {
    World world;
    auto& registry    = world.registry;
    const auto& slots = World::slots();

    world.tick();

    {  // unary: an entity passing where: carries the fact with this run's value
        REQUIRE(world.has<game::LowHealth>(slots.Wounded));
        CHECK(registry.get<game::LowHealth>(slots.Wounded).missing == 80.0F);
        CHECK_FALSE(world.has<game::LowHealth>(slots.Healthy));
        CHECK(world.log().lows == 2);
    }

    {  // unary: in, patched, then out
        registry.get<game::Health>(slots.Healthy).current = 25.0F;
        world.tick();
        REQUIRE(world.has<game::LowHealth>(slots.Healthy));
        CHECK(world.log().lows == 3);
        registry.get<game::Health>(slots.Healthy).current = 15.0F;
        world.tick();
        CHECK(registry.get<game::LowHealth>(slots.Healthy).missing == 85.0F);
        CHECK(world.log().lows == 3);
        registry.get<game::Health>(slots.Healthy).current = 50.0F;
        world.tick();
        CHECK_FALSE(world.has<game::LowHealth>(slots.Healthy));
        CHECK(world.log().recoveries == 1);
    }

    {  // unary: an entity leaving the domain loses the fact
        REQUIRE(world.has<game::LowHealth>(slots.Leaver));
        registry.remove<game::Health>(slots.Leaver);
        world.tick();
        CHECK_FALSE(world.has<game::LowHealth>(slots.Leaver));
        CHECK(world.log().recoveries == 2);
    }

    {  // unreduced pair: two witnesses give one fact
        CHECK_FALSE(world.has<game::Caught>(slots.Sprinter));
        registry.get<game::Runner>(slots.Sprinter).x = 10.5F;
        world.tick();
        REQUIRE(world.has<game::Caught>(slots.Sprinter));
        CHECK(world.log().caught == 1);
        registry.get<game::Runner>(slots.Sprinter).x = 9.5F;
        world.tick();
        CHECK(world.has<game::Caught>(slots.Sprinter));
        CHECK(world.log().caught == 1);
        CHECK(world.log().released == 0);
    }

    {  // unreduced pair: losing the last witness ends the fact
        registry.get<game::Runner>(slots.Sprinter).x = 5.0F;
        world.tick();
        CHECK_FALSE(world.has<game::Caught>(slots.Sprinter));
        CHECK(world.log().released == 1);
    }

    {  // condition: an empty group is checked too
        CHECK(world.has<game::EmptyBag>(slots.Pack));
        CHECK_FALSE(world.has<game::EmptyBag>(slots.Pouch));
        const int emptied = world.log().emptied;
        registry.get<game::Item>(slots.Coin).bag = 1;
        world.tick();
        CHECK_FALSE(world.has<game::EmptyBag>(slots.Pack));
        CHECK(world.has<game::EmptyBag>(slots.Pouch));
        CHECK(world.log().filled == 1);
        CHECK(world.log().emptied == emptied + 1);
    }

    {  // threshold: two allies are not enough, three are
        CHECK_FALSE(world.has<game::Supported>(slots.First));
        registry.get<game::Soldier>(slots.Straggler).x = 2.0F;
        world.tick();
        REQUIRE(world.has<game::Supported>(slots.First));
        CHECK(registry.get<game::Supported>(slots.First).allies == 3);
        CHECK(world.has<game::Supported>(slots.Second));
        CHECK(world.has<game::Supported>(slots.Third));
        CHECK(world.has<game::Supported>(slots.Straggler));
        CHECK(world.log().supported == 4);
    }

    {  // all conditions false: every carrier loses the fact
        float x = 0.0F;
        for (const auto soldier : {slots.First, slots.Second, slots.Third, slots.Straggler}) {
            registry.get<game::Soldier>(soldier).x = x;
            x += 10.0F;
        }
        world.tick();
        for (const auto soldier : {slots.First, slots.Second, slots.Third, slots.Straggler}) {
            CHECK_FALSE(world.has<game::Supported>(soldier));
        }
        CHECK(world.log().unsupported == 4);
    }

    {  // when: false pauses updates; the fact keeps its presence and value
        REQUIRE(world.has<game::LowHealth>(slots.Wounded));
        registry.get<game::Control>(slots.Settings).frozen = true;
        registry.get<game::Health>(slots.Wounded).current = 90.0F;
        world.tick();
        CHECK(world.has<game::LowHealth>(slots.Wounded));
        CHECK(registry.get<game::LowHealth>(slots.Wounded).missing == 80.0F);
        registry.get<game::Control>(slots.Settings).frozen = false;
        world.tick();
        CHECK_FALSE(world.has<game::LowHealth>(slots.Wounded));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
