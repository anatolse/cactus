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

using Health  = cactus_gen::disjoint_rule_domains::Health;
using Calm    = cactus_gen::disjoint_rule_domains::Mood::Calm;
using Curious = cactus_gen::disjoint_rule_domains::Mood::Curious;
using Hunting = cactus_gen::disjoint_rule_domains::Mood::Hunting;

struct World {
    entt::registry registry;
    entt::entity knight;
    entt::entity orc;
    entt::entity watchman;

    World() {
        cactus_headless_test::init_and_load(registry);
        const auto& slots = cactus_gen::detail::generated_named_slots();
        knight            = slots.disjoint_rule_domains.Knight;
        orc               = slots.disjoint_rule_domains.Orc;
        watchman          = slots.disjoint_rule_domains.Watchman;
    }

    void frame() { cactus_headless_test::drive_frame(registry); }

    [[nodiscard]] int hp(entt::entity entity) const { return registry.get<Health>(entity).hp; }
};

}  // namespace

TEST_CASE("rules over disjoint archetypes assign one field without an order", "[runtime][disjoint-domains]") {
    World world;
    REQUIRE(world.hp(world.knight) == 5);
    REQUIRE(world.hp(world.orc) == 5);

    for (int frame = 0; frame < 3; ++frame) {
        world.frame();
    }
    CHECK(world.hp(world.knight) == 10);
    CHECK(world.hp(world.orc) == 2);

    for (int frame = 0; frame < 4; ++frame) {
        world.frame();
    }
    CHECK(world.hp(world.knight) == 10);
    CHECK(world.hp(world.orc) == 0);
}

TEST_CASE("per-variant rules move a state along its arrows on their own row", "[runtime][disjoint-domains]") {
    World world;
    auto& registry = world.registry;
    REQUIRE(registry.all_of<Calm>(world.watchman));

    world.frame();
    CHECK(registry.all_of<Calm>(world.watchman));
    world.frame();
    CHECK(registry.all_of<Curious>(world.watchman));
    world.frame();
    CHECK(registry.all_of<Curious>(world.watchman));
    world.frame();
    CHECK(registry.all_of<Hunting>(world.watchman));
    world.frame();
    CHECK(registry.all_of<Hunting>(world.watchman));
    world.frame();
    REQUIRE(registry.all_of<Calm>(world.watchman));
    CHECK(registry.get<Calm>(world.watchman).rest == 0);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
