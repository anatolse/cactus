// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

namespace {

namespace fixture = cactus_gen::hierarchy_child_loop_runtime;
using cactus::runtime::entt_backend::CreationOrdinal;

struct World {
    entt::registry registry;
    entt::entity box{entt::null};

    World() {
        cactus_headless_test::init_and_load(registry);
        const auto boxes = registry.view<fixture::Box>();
        REQUIRE(boxes.begin() != boxes.end());
        box = *boxes.begin();
    }

    entt::entity create() {
        const auto entity = registry.create();
        registry.emplace<CreationOrdinal>(
            entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
        return entity;
    }

    entt::entity item() {
        const auto entity = create();
        registry.emplace<fixture::Item>(entity);
        registry.emplace<cactus_gen::stdlib::core::Parent>(entity, cactus_gen::stdlib::core::Parent{.parent = box});
        return entity;
    }

    void inject(auto occurrence) {
        cactus::runtime::entt_backend::generated_inject_external_event(occurrence);
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
    }

    [[nodiscard]] std::size_t children() {
        std::size_t count = 0;
        cactus::runtime::entt_backend::hierarchy_index<cactus_gen::stdlib::core::Parent>(registry).for_each_child(
            box, [&](entt::entity) { ++count; });
        return count;
    }
};

}  // namespace

TEST_CASE("hierarchy child loop headless: a spawn inside the loop doesn't change the iteration",
          "[runtime][query][hierarchy]") {
    World world;
    world.item();
    world.item();

    world.inject(fixture::GrowEvent{});
    CHECK(world.registry.get<fixture::Box>(world.box).visits == 2);
    CHECK(world.children() == 4);

    world.inject(fixture::GrowEvent{});
    CHECK(world.registry.get<fixture::Box>(world.box).visits == 6);
    CHECK(world.children() == 8);
}

TEST_CASE("hierarchy child loop headless: a projected filter trait keeps the snapshot",
          "[runtime][query][hierarchy]") {
    World world;
    const auto first = world.item();
    world.registry.emplace<fixture::Marked>(first);
    world.item();
    world.item();

    world.inject(fixture::MarkEvent{});
    CHECK(world.registry.get<fixture::Box>(world.box).marked == 1);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
