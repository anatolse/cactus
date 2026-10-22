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

namespace {

using Results = cactus_gen::spatial_query_ties::Results;
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

    // The newest entity, which the last make event created.
    entt::entity newest() {
        entt::entity found = entt::null;
        std::uint64_t ordinal{};
        for (const auto [entity, created] : registry.view<CreationOrdinal>().each()) {
            if (found == entt::null || created.value > ordinal) {
                found   = entity;
                ordinal = created.value;
            }
        }
        return found;
    }

    entt::entity enemy(float x, float y) {
        inject(cactus_gen::spatial_query_ties::MakeEnemyEvent{.x = x, .y = y});
        return newest();
    }

    entt::entity wall(float x, float y) {
        inject(cactus_gen::spatial_query_ties::MakeWallEvent{.x = x, .y = y});
        return newest();
    }

    entt::entity crate(float x, float y, int id) {
        inject(cactus_gen::spatial_query_ties::MakeCrateEvent{.x = x, .y = y, .id = id});
        return newest();
    }

    entt::entity enemy_3d(float x, float y, float z) {
        inject(cactus_gen::spatial_query_ties::MakeEnemy3DEvent{.x = x, .y = y, .z = z});
        return newest();
    }

    entt::entity wall_3d(float x, float y, float z) {
        inject(cactus_gen::spatial_query_ties::MakeWall3DEvent{.x = x, .y = y, .z = z});
        return newest();
    }

    // Transforms propagate in tick, so a frame runs before the probe.
    const Results& probe() {
        inject(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        inject(cactus_gen::spatial_query_ties::ProbeEvent{});
        return registry.get<Results>(cactus_gen::detail::generated_named_slots().spatial_query_ties.Board);
    }

    [[nodiscard]] int place(entt::entity crate) const {
        return registry.get<cactus_gen::spatial_query_ties::Rank>(crate).place;
    }

    // The place of the crate labelled `id`.
    [[nodiscard]] int place_of(int id) const {
        for (const auto [crate, label] : registry.view<cactus_gen::spatial_query_ties::Label>().each()) {
            if (label.id == id) {
                return place(crate);
            }
        }
        return 0;
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("spatial queries break ties and order lists by creation", "[runtime][spatial-query][native-result-ordering]") {
    World world;

    const auto a = world.enemy(5.0F, 0.0F);
    const auto b = world.enemy(-5.0F, 0.0F);
    world.enemy(0.0F, 5.0F);
    const auto wall_a = world.wall(10.0F, 0.25F);
    world.wall(10.0F, -0.25F);
    const auto enemy_3d = world.enemy_3d(0.0F, 0.0F, 5.0F);
    world.enemy_3d(0.0F, 0.0F, -5.0F);
    const auto wall_3d = world.wall_3d(10.0F, 0.25F, 0.0F);
    world.wall_3d(10.0F, -0.25F, 0.0F);
    const auto c1 = world.crate(1.0F, 1.0F, 1);
    const auto c2 = world.crate(2.0F, 2.0F, 2);
    const auto c3 = world.crate(3.0F, 3.0F, 3);

    // Exact ties go to the oldest entity.
    const auto& results = world.probe();
    CHECK(results.nearest == a);
    CHECK(results.ray == wall_a);
    CHECK(results.nearest_3d == enemy_3d);
    CHECK(results.ray_3d == wall_3d);
    CHECK(world.place(c1) == 1);
    CHECK(world.place(c2) == 2);
    CHECK(world.place(c3) == 3);

    // Removing and adding a trait back moves storage, not creation order.
    for (const auto churned : {b, a}) {
        world.inject(cactus_gen::spatial_query_ties::UnmarkEvent{.target = churned});
        world.inject(cactus_gen::spatial_query_ties::RemarkEvent{.target = churned});
    }
    CHECK(world.probe().nearest == a);

    // A destroy and a new crate keep the list in creation order.
    world.inject(cactus_gen::spatial_query_ties::DiscardEvent{.target = c1});
    const auto c4 = world.crate(4.0F, 4.0F, 4);
    world.probe();
    CHECK(world.place(c2) == 1);
    CHECK(world.place(c3) == 2);
    CHECK(world.place(c4) == 3);

    // A save and restore recreates every entity; the list keeps its order.
    auto& registry      = world.registry;
    const auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
    REQUIRE(cactus::runtime::entt_backend::generated_restore_world(registry, snapshot).ok);
    world.probe();
    CHECK(world.place_of(2) == 1);
    CHECK(world.place_of(3) == 2);
    CHECK(world.place_of(4) == 3);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
