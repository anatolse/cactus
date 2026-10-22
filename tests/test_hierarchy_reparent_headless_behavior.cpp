// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace {

using Parent  = cactus_gen::stdlib::core::Parent;
using Pickups = cactus_gen::hierarchy_reparent::Pickups;
using World_  = cactus_gen::stdlib::transform::flat::WorldTransform;
using cactus::runtime::entt_backend::CreationOrdinal;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    void inject(auto occurrence) {
        cactus::runtime::entt_backend::generated_inject_external_event(occurrence);
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
    }

    void frame() {
        inject(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    }

    // The newest entity carrying `Marker`.
    template <typename Marker>
    entt::entity newest() {
        entt::entity found = entt::null;
        std::uint64_t ordinal{};
        for (const auto entity : registry.view<Marker>()) {
            const auto value = registry.get<CreationOrdinal>(entity).value;
            if (found == entt::null || value > ordinal) {
                found   = entity;
                ordinal = value;
            }
        }
        REQUIRE(found != entt::entity{entt::null});
        return found;
    }

    entt::entity hand(float x) {
        inject(cactus_gen::hierarchy_reparent::MakeHandEvent{.x = x});
        return newest<cactus_gen::hierarchy_reparent::Hand>();
    }

    entt::entity item(float x) {
        inject(cactus_gen::hierarchy_reparent::MakeItemEvent{.x = x});
        return newest<cactus_gen::hierarchy_reparent::Item>();
    }

    void pick_up(entt::entity item, entt::entity hand) {
        inject(cactus_gen::hierarchy_reparent::PickUpEvent{.item = item, .hand = hand});
    }

    [[nodiscard]] entt::entity parent_of(entt::entity entity) const {
        const auto* parent = registry.try_get<Parent>(entity);
        return parent == nullptr ? entt::entity{entt::null} : parent->parent;
    }

    [[nodiscard]] int pickups(entt::entity entity) const {
        return registry.get<Pickups>(entity).count;
    }

    [[nodiscard]] float world_x(entt::entity entity) const {
        return registry.get<World_>(entity).position.x;
    }
};

}  // namespace

TEST_CASE("hierarchy reparent: a picked-up item follows the hand until dropped",
          "[runtime][codegen-entt][hierarchy-forest]") {
    World world;
    const auto hand = world.hand(10.0F);
    const auto item = world.item(1.0F);
    world.frame();
    CHECK(world.world_x(item) == 1.0F);

    world.pick_up(item, hand);
    CHECK(world.parent_of(item) == hand);
    CHECK(world.pickups(item) == 1);
    world.frame();
    CHECK(world.world_x(item) == 11.0F);
    CHECK(world.registry.get<World_>(item).position.y == 1.0F);

    world.inject(cactus_gen::hierarchy_reparent::MoveHandEvent{.hand = hand, .x = 20.0F});
    world.frame();
    CHECK(world.world_x(item) == 21.0F);

    world.inject(cactus_gen::hierarchy_reparent::DropEvent{.item = item});
    CHECK_FALSE(world.registry.all_of<Parent>(item));
    world.frame();
    CHECK(world.world_x(item) == 1.0F);
}

TEST_CASE("hierarchy reparent: moving a carried item fires nothing", "[runtime][codegen-entt][hierarchy-forest]") {
    World world;
    const auto left  = world.hand(10.0F);
    const auto right = world.hand(100.0F);
    const auto item  = world.item(1.0F);
    world.pick_up(item, left);
    world.pick_up(item, right);
    CHECK(world.parent_of(item) == right);
    CHECK(world.pickups(item) == 1);
    world.frame();
    CHECK(world.world_x(item) == 101.0F);
}

TEST_CASE("hierarchy reparent: a reparent under its own descendant does nothing",
          "[runtime][codegen-entt][hierarchy-forest]") {
    World world;
    const auto top    = world.item(1.0F);
    const auto middle = world.item(2.0F);
    const auto bottom = world.item(3.0F);
    world.pick_up(middle, top);
    world.pick_up(bottom, middle);
    REQUIRE(world.pickups(middle) == 1);

    world.pick_up(top, bottom);
    CHECK_FALSE(world.registry.all_of<Parent>(top));
    CHECK(world.pickups(top) == 0);

    world.pick_up(top, top);
    CHECK_FALSE(world.registry.all_of<Parent>(top));
    CHECK(world.pickups(top) == 0);

    world.frame();
    CHECK(world.world_x(bottom) == 6.0F);
}

TEST_CASE("hierarchy reparent: of two reparents forming a cycle in one commit, the second does nothing",
          "[runtime][codegen-entt][hierarchy-forest]") {
    World world;
    const auto a = world.item(1.0F);
    const auto b = world.item(2.0F);
    world.inject(cactus_gen::hierarchy_reparent::SwapEvent{.a = a, .b = b});
    CHECK(world.parent_of(a) == b);
    CHECK_FALSE(world.registry.all_of<Parent>(b));
    CHECK(world.pickups(a) == 1);
    CHECK(world.pickups(b) == 0);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
