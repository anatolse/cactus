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

namespace game = cactus_gen::keep_chains;

constexpr float kDt = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    // One frame: one fixed tick, then one tick.
    void frame() {
        const int before = ticks();
        cactus_headless_test::drive_frame(registry, kDt);
        REQUIRE(ticks() == before + 1);
    }

    int ticks() {
        return registry.get<game::Tally>(cactus_gen::detail::generated_named_slots().keep_chains.Clock).ticks;
    }

    game::Cell& cell(entt::entity entity) {
        return registry.get<game::Cell>(entity);
    }

    bool anchored(entt::entity entity) {
        return registry.all_of<game::Anchored>(entity);
    }

    bool floating(entt::entity entity) {
        return registry.all_of<game::Floating>(entity);
    }

    bool shown(entt::entity entity) {
        return registry.all_of<game::Shown>(entity);
    }

    bool held(entt::entity cell, entt::entity root) {
        return cactus::runtime::entt_backend::relation_store<game::Held>(registry).contains(cell, root);
    }

    void freeze(bool frozen) {
        registry.patch<game::Control>(cactus_gen::detail::generated_named_slots().keep_chains.Settings, [&](game::Control& cactus_value) { cactus_value.frozen = frozen; });
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("a keep chain settles in one activation", "[runtime][rule-keep][keep-chains]") {
    World world;
    const auto& slots = cactus_gen::detail::generated_named_slots().keep_chains;
    const auto base   = slots.Base;
    const auto near   = slots.Near;
    const auto edge   = slots.Edge;
    const auto loose  = slots.Loose;

    {  // the first activation computes the whole chain
        world.frame();
        CHECK(world.anchored(base));
        CHECK(world.held(near, base));
        CHECK(world.held(edge, base));
        CHECK_FALSE(world.floating(base));
        CHECK_FALSE(world.floating(near));
        CHECK_FALSE(world.floating(edge));
        CHECK(world.floating(loose));
    }

    {  // on added fires only for the cell that qualifies
        CHECK(world.cell(base).falls == 0);
        CHECK(world.cell(near).falls == 0);
        CHECK(world.cell(edge).falls == 0);
        CHECK(world.cell(loose).falls == 1);
    }

    {  // another phase reads what the fixed tick committed
        CHECK(world.shown(loose));
        CHECK_FALSE(world.shown(near));
    }

    {  // a change to the base reaches the last fact after one activation
        world.registry.patch<game::Cell>(base, [&](game::Cell& cactus_value) { cactus_value.grounded = false; });
        world.frame();
        CHECK_FALSE(world.anchored(base));
        CHECK_FALSE(world.held(near, base));
        CHECK(world.floating(base));
        CHECK(world.floating(near));
        CHECK(world.floating(edge));
        CHECK(world.cell(near).falls == 1);
        CHECK(world.cell(edge).falls == 1);
        CHECK(world.shown(near));
    }

    {  // and back
        world.registry.patch<game::Cell>(base, [&](game::Cell& cactus_value) { cactus_value.grounded = true; });
        world.frame();
        CHECK(world.anchored(base));
        CHECK_FALSE(world.floating(base));
        CHECK_FALSE(world.floating(near));
        CHECK_FALSE(world.floating(edge));
        CHECK(world.floating(loose));
        CHECK(world.cell(near).falls == 1);
        CHECK(world.cell(loose).falls == 1);
        CHECK_FALSE(world.shown(near));
    }

    {  // a gated keeper keeps its rows, and the rules after it still run
        world.freeze(true);
        world.registry.patch<game::Cell>(base, [&](game::Cell& cactus_value) { cactus_value.grounded = false; });
        world.registry.patch<game::Cell>(edge, [&](game::Cell& cactus_value) { cactus_value.x = 20; });
        world.frame();
        CHECK(world.anchored(base));
        CHECK(world.held(near, base));
        CHECK_FALSE(world.held(edge, base));
        CHECK_FALSE(world.floating(near));
        CHECK(world.floating(edge));
        CHECK(world.cell(edge).falls == 2);
    }

    {  // the keeper runs again once the gate opens
        world.freeze(false);
        world.frame();
        CHECK_FALSE(world.anchored(base));
        CHECK(world.floating(base));
        CHECK(world.floating(near));
        CHECK(world.cell(near).falls == 2);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
