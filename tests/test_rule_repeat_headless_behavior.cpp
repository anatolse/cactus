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

namespace game = cactus_gen::rule_repeat;

struct World {
    entt::registry registry;

    World() { cactus_headless_test::init_and_load(registry); }

    void frame() { cactus_headless_test::drive_frame(registry); }

    template <typename Trait>
    Trait& of(entt::entity entity) {
        return registry.get<Trait>(entity);
    }
};

const auto& slots() {
    return cactus_gen::detail::generated_named_slots().rule_repeat;
}

}  // namespace

TEST_CASE("a repeating rule runs its pass exactly N times per activation", "[runtime][rule-repeat]") {
    World world;
    world.frame();
    CHECK(world.of<game::Counter>(slots().Clicker).count == 6);
    world.frame();
    CHECK(world.of<game::Counter>(slots().Clicker).count == 12);
}

TEST_CASE("a repeating rule evaluates where: again in every pass", "[runtime][rule-repeat]") {
    World world;
    world.frame();
    CHECK(world.of<game::Settler>(slots().Calm).steps == 1);
    CHECK(world.of<game::Settler>(slots().Calm).done);
}

TEST_CASE("a repeating rule evaluates when: again in every pass", "[runtime][rule-repeat]") {
    World world;
    world.frame();
    CHECK(world.of<game::Pulse>(slots().Beat).count == 1);
    CHECK_FALSE(world.of<game::Switch>(slots().Gate).open);
}

TEST_CASE("each pass groups its reduce rows from the world the previous pass left", "[runtime][rule-repeat]") {
    World world;
    world.frame();
    const auto& runner = world.of<game::Mover>(slots().Runner);
    CHECK(runner.x == 3.0F);
    CHECK(runner.seen == 3 + 2 + 1);
}

TEST_CASE("accumulated deltas of every pass reach a later reader", "[runtime][rule-repeat]") {
    World world;
    world.frame();
    const auto& heap = world.of<game::Heap>(slots().Pile);
    CHECK(heap.total == (3.0F * (1.0F + 2.0F)) + 20.0F);
    CHECK(heap.seen == heap.total);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
