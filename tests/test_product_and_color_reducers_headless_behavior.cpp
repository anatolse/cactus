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

namespace {

namespace game = cactus_gen::product_and_color_reducers;

struct World {
    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().product_and_color_reducers;
    }

    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] const game::Look& look(entt::entity entity) const {
        return registry.get<game::Look>(entity);
    }
};

bool same(CactusColor a, CactusColor b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Keeps each step in binary32.
float product_in_order(float a, float b, float c) {
    const volatile float first = a * b;
    return first * c;
}

}  // namespace

TEST_CASE("scale factors multiply", "[runtime][rule-reduce][product]") {
    const World world;
    const auto& hero = world.look(World::slots().Hero);
    CHECK(hero.scale == product_in_order(1.0F, 1.2F, 0.9F));
    CHECK(hero.boosted == hero.scale);
    CHECK(hero.stacks == -6);
}

TEST_CASE("a target with no effects gets the neutral values", "[runtime][rule-reduce][product]") {
    const World world;
    const auto& idle = world.look(World::slots().Idle);
    CHECK(idle.scale == 1.0F);
    CHECK(same(idle.tint, CactusColor{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F}));
    CHECK(same(idle.flash, CactusColor{}));
    CHECK(idle.stacks == 1);
    CHECK(idle.boosted == 2.0F);
}

TEST_CASE("a color product multiplies a fade and a tint per channel", "[runtime][rule-reduce][product]") {
    const World world;
    CHECK(same(world.look(World::slots().Hero).tint,
               CactusColor{.r = 2.0F, .g = 0.5F, .b = 0.5F, .a = 0.5F}));
}

TEST_CASE("a color sum keeps channels above 1", "[runtime][rule-reduce][color-sum]") {
    const World world;
    CHECK(same(world.look(World::slots().Hero).flash,
               CactusColor{.r = 1.5F, .g = 1.5F, .b = 1.5F, .a = 1.5F}));
}

TEST_CASE("a float product folds in creation order", "[runtime][rule-reduce][product]") {
    const auto in_order = product_in_order(1.1F, 1.3F, 0.3F);
    REQUIRE(in_order != product_in_order(0.3F, 1.3F, 1.1F));
    const World world;
    CHECK(world.look(World::slots().Ordered).scale == in_order);
}

TEST_CASE("products and color sums don't depend on the schedule", "[runtime][rule-reduce][product]") {
    cactus::test::require_linearizations_match(3, cactus::test::no_setup());
    (void)cactus::test::require_pair_swaps_match(3, cactus::test::no_setup());
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
