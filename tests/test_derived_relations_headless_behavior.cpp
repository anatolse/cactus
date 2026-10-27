// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace game = cactus_gen::derived_relations;
using cactus::runtime::entt_backend::derived_relation;
using cactus::runtime::entt_backend::relation_store;

constexpr float kDt = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    [[nodiscard]] static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().derived_relations;
    }
    int ticks() {
        return registry.get<game::Ticks>(slots().Clock).count;
    }
    // Runs `count` fixed ticks, one per 1/60 s frame.
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            const int before = ticks();
            cactus_headless_test::drive_frame(registry, kDt);
            REQUIRE(ticks() == before + 1);
        }
    }
    [[nodiscard]] bool connected(entt::entity tile) const {
        return registry.all_of<game::Connected>(tile);
    }
    [[nodiscard]] std::optional<int> room_size(entt::entity tile) const {
        const auto* size = registry.try_get<game::RoomSize>(tile);
        return size == nullptr ? std::nullopt : std::optional(size->size);
    }
    // The `Route` row (from, to) as (next, distance).
    std::optional<std::pair<entt::entity, int>> route(entt::entity from, entt::entity to) {
        std::optional<std::pair<entt::entity, int>> found;
        derived_relation<game::Route>(registry).for_each_to(to, [&](const auto& entry) {
            if (entry.from == from) {
                found.emplace(entry.row.next, entry.row.distance);
            }
        });
        return found;
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("derived relations: closure, shortest and components", "[runtime][relations][derived]") {
    World world;
    auto& registry    = world.registry;
    const auto& slots = World::slots();

    world.tick(2);

    {  // every floor tile links lead to from the heart is connected
        for (const auto tile : {slots.Heart,
                                slots.East,
                                slots.FarEast,
                                slots.North,
                                slots.EastSide,
                                slots.FarNorth,
                                slots.Top,
                                slots.Goal}) {
            CHECK(world.connected(tile));
        }
        CHECK_FALSE(world.connected(slots.Wall));
        CHECK_FALSE(world.connected(slots.Island));
        CHECK(derived_relation<game::Reach>(registry).contains(slots.Goal, slots.Heart));
    }

    {  // a keep rule reads the links the last run committed, so Connected follows one tick later
        registry.emplace<game::Floor>(slots.Wall);
        world.tick();
        CHECK(derived_relation<game::Reach>(registry).contains(slots.Heart, slots.Wall));
        CHECK_FALSE(world.connected(slots.Wall));
        world.tick();
        CHECK(world.connected(slots.Wall));
        registry.remove<game::Floor>(slots.Wall);
        world.tick(2);
        CHECK_FALSE(world.connected(slots.Wall));
    }

    {  // cutting the heart's links disconnects everything but the heart
        registry.remove<game::Floor>(slots.East);
        registry.remove<game::Floor>(slots.North);
        world.tick(2);
        CHECK(world.connected(slots.Heart));
        CHECK_FALSE(world.connected(slots.FarEast));
        CHECK_FALSE(world.connected(slots.Goal));
        registry.emplace<game::Floor>(slots.East);
        registry.emplace<game::Floor>(slots.North);
        world.tick(2);
        CHECK(world.connected(slots.Goal));
    }

    {  // two equal routes around the wall: the step through the older tile wins
        CHECK(world.route(slots.Heart, slots.Goal) == std::pair{slots.East, 4});
        auto& runner  = registry.get<game::Walker>(slots.Runner);
        runner.at     = slots.Heart;
        runner.target = slots.Goal;
        auto& stuck   = registry.get<game::Walker>(slots.Stuck);
        stuck.at      = slots.Heart;
        stuck.target  = slots.Island;
        world.tick();
        CHECK(registry.get<game::Walker>(slots.Runner).at == slots.East);
        world.tick(3);
        CHECK(registry.get<game::Walker>(slots.Runner).at == slots.Goal);
        world.tick();
        CHECK(registry.get<game::Walker>(slots.Runner).at == slots.Goal);
        CHECK(registry.get<game::Walker>(slots.Runner).steps == 4);
    }

    {  // a tick that changes no link rebuilds no route
        const auto builds = derived_relation<game::Route>(registry).builds();
        world.tick();
        CHECK(derived_relation<game::Route>(registry).builds() == builds);
    }

    {  // an unreachable goal has no route, and its walker never moves
        CHECK_FALSE(world.route(slots.Heart, slots.Island).has_value());
        CHECK(registry.get<game::Walker>(slots.Stuck).at == slots.Heart);
        CHECK(registry.get<game::Walker>(slots.Stuck).steps == 0);
    }

    {  // a dearer tile moves the cheapest route to the other side
        registry.get<game::Tile>(slots.East).cost = 5;
        world.tick();
        CHECK(world.route(slots.Heart, slots.Goal) == std::pair{slots.North, 4});
        CHECK(world.route(slots.FarNorth, slots.FarEast) == std::pair{slots.Top, 4});
        auto& runner  = registry.get<game::Walker>(slots.Runner);
        runner.at     = slots.Heart;
        world.tick();
        CHECK(registry.get<game::Walker>(slots.Runner).at == slots.North);
    }

    {  // two rooms, each named by its oldest tile and sized
        auto& room = derived_relation<game::Room>(registry);
        CHECK(room.contains(slots.L1, slots.L0));
        CHECK(room.contains(slots.L4, slots.L3));
        CHECK_FALSE(room.has_from(slots.L2));
        CHECK(world.room_size(slots.L0) == 2);
        CHECK(world.room_size(slots.L3) == 2);
        CHECK_FALSE(world.room_size(slots.L1).has_value());
        CHECK_FALSE(world.room_size(slots.L4).has_value());
    }

    {  // a joining tile merges the rooms under the older name
        registry.emplace<game::Lair>(slots.L2);
        world.tick(2);
        CHECK(derived_relation<game::Room>(registry).contains(slots.L4, slots.L0));
        CHECK(world.room_size(slots.L0) == 5);
        CHECK_FALSE(world.room_size(slots.L3).has_value());
    }

    {  // a removed tile splits the room: the part without the old name takes its oldest tile
        registry.remove<game::Lair>(slots.L1);
        world.tick(2);
        auto& room = derived_relation<game::Room>(registry);
        CHECK(room.contains(slots.L0, slots.L0));
        CHECK(room.contains(slots.L4, slots.L2));
        CHECK(world.room_size(slots.L0) == 1);
        CHECK(world.room_size(slots.L2) == 3);
    }

    {  // portals: the closure of a stored relation
        auto& portals = relation_store<game::Portal>(registry);
        portals.add(slots.FirstGate, slots.SecondGate, game::Portal{});
        portals.add(slots.SecondGate, slots.ThirdGate, game::Portal{});
        world.tick();
        CHECK_FALSE(registry.all_of<game::Linked>(slots.FirstGate));
        CHECK(registry.all_of<game::Linked>(slots.SecondGate));
        CHECK(registry.all_of<game::Linked>(slots.ThirdGate));
    }

    {  // a restored world reads its restored links right away
        const auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
        std::vector<std::string_view> recorded;
        for (const auto& row : snapshot.relation_rows()) {
            recorded.push_back(snapshot.relation_name(row));
        }
        // Derived and kept rows are never saved: only the two stored portals are.
        CHECK(recorded == std::vector<std::string_view>{"derived_relations.Portal", "derived_relations.Portal"});
        relation_store<game::Portal>(registry).erase(slots.SecondGate, slots.ThirdGate);
        CHECK_FALSE(derived_relation<game::PortalReach>(registry).contains(slots.FirstGate, slots.ThirdGate));
        const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
        REQUIRE(outcome.ok);
        CHECK(derived_relation<game::PortalReach>(registry).contains(slots.FirstGate, slots.ThirdGate));
        CHECK(derived_relation<game::PortalReach>(registry).has_to(slots.SecondGate));
        CHECK_FALSE(derived_relation<game::PortalReach>(registry).has_to(slots.FirstGate));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
