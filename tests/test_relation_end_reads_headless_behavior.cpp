#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

namespace game    = cactus_gen::relation_end_reads;
namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
        tick();
        tick();
    }

    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().relation_end_reads;
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    int alerts(entt::entity watcher) {
        return registry.get<game::Watcher>(watcher).alerts;
    }

    int lit(entt::entity lamp) {
        return registry.get<game::Lamp>(lamp).lit;
    }

    const auto& sees() {
        return backend::relation_store<game::Sees>(registry);
    }
};

TEST_CASE("A row rule filters by a field of each end", "[headless][relations][end-reads]") {
    World world;
    const auto& slots = World::slots();
    CHECK(world.sees().size() == 4);
    const auto owl = world.alerts(slots.Owl);
    const auto cat = world.alerts(slots.Cat);
    world.tick();
    // Owl (range 5) hears only Bell (8); Cat (range 1) hears Mouse (3) and Bell.
    CHECK(world.alerts(slots.Owl) == owl + 1);
    CHECK(world.alerts(slots.Cat) == cat + 2);
}

TEST_CASE("A pair rule reads a row end against a joined binding", "[headless][relations][end-reads]") {
    World world;
    const auto& slots = World::slots();
    const auto mouse  = world.lit(slots.Mouse);
    const auto bell   = world.lit(slots.Bell);
    world.tick();
    // Only Owl reaches past either lamp's power.
    CHECK(world.lit(slots.Mouse) == mouse + 1);
    CHECK(world.lit(slots.Bell) == bell + 1);
}

TEST_CASE("A row goes when its end loses the end trait", "[headless][relations][end-reads]") {
    World world;
    const auto& slots = World::slots();
    world.registry.get<game::Control>(slots.Settings).fade = 1;
    world.tick();
    CHECK_FALSE(world.registry.all_of<game::Target>(slots.Mouse));
    CHECK_FALSE(world.sees().contains(slots.Owl, slots.Mouse));
    CHECK_FALSE(world.sees().contains(slots.Cat, slots.Mouse));
    CHECK(world.sees().size() == 2);
    const auto cat   = world.alerts(slots.Cat);
    const auto mouse = world.lit(slots.Mouse);
    world.tick();
    CHECK(world.alerts(slots.Cat) == cat + 1);
    CHECK(world.lit(slots.Mouse) == mouse);
}

}  // namespace
