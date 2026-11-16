// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

namespace game = cactus_gen::reduced_relation_keep;

constexpr float kDt = 1.0F / 60.0F;

struct Row {
    entt::entity from;
    entt::entity to;
    int highest;
    int touched;

    bool operator==(const Row&) const = default;
};

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    void tick() {
        const int before = ticks();
        cactus_headless_test::drive_frame(registry, kDt);
        REQUIRE(ticks() == before + 1);
        REQUIRE(store().one_ends_hold());
    }

    int ticks() {
        return registry.get<game::Tally>(cactus_gen::detail::generated_named_slots().reduced_relation_keep.Clock).ticks;
    }

    cactus::runtime::entt_backend::RelationStore<game::AtCheckpoint>& store() {
        return cactus::runtime::entt_backend::relation_store<game::AtCheckpoint>(registry);
    }

    // Live rows in row order.
    std::vector<Row> rows() {
        std::vector<Row> result;
        for (const auto& entry : store().rows()) {
            result.push_back({entry.from, entry.to, entry.row.highest, entry.row.touched});
        }
        return result;
    }

    // The winner the old form keeps.
    entt::entity reached(entt::entity player) {
        return registry.get<game::Reached>(player).checkpoint;
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("a reduced relation keep holds one row per group to its best winner", "[runtime][rule-keep][relations]") {
    World world;
    auto& registry    = world.registry;
    const auto& slots = cactus_gen::detail::generated_named_slots().reduced_relation_keep;

    {  // nothing is kept before the first run
        CHECK(world.rows().empty());
    }

    {  // first winner: one row per group with rows, in the players' creation order
        world.tick();
        CHECK(world.rows() == std::vector<Row>{{slots.Lead, slots.Third, 3, 2}, {slots.Second, slots.Third, 3, 1}});
        CHECK(world.reached(slots.Lead) == slots.Third);
        CHECK(world.reached(slots.Second) == slots.Third);
    }

    {  // an empty group holds no row
        CHECK_FALSE(world.store().has_from(slots.Idler));
        CHECK_FALSE(registry.all_of<game::Reached>(slots.Idler));
    }

    {  // the same winner with new fields keeps its row in place
        registry.patch<game::Player>(slots.Second, [&](game::Player& cactus_value) { cactus_value.x = -0.2F; });
        world.tick();
        CHECK(world.rows() == std::vector<Row>{{slots.Lead, slots.Third, 3, 2}, {slots.Second, slots.Third, 3, 2}});
    }

    {  // a new winner removes the old row and adds a new one at the end
        registry.patch<game::Checkpoint>(slots.Fifth, [&](game::Checkpoint& cactus_value) { cactus_value.x = 1.0F; });
        world.tick();
        CHECK(world.rows() == std::vector<Row>{{slots.Second, slots.Third, 3, 2}, {slots.Lead, slots.Fifth, 5, 3}});
        CHECK(world.reached(slots.Lead) == slots.Fifth);
    }

    {  // a destroyed winner takes its rows with it; the next run picks the next best
        registry.destroy(slots.Third);
        CHECK_FALSE(world.store().has_from(slots.Second));
        world.tick();
        CHECK(world.rows() == std::vector<Row>{{slots.Lead, slots.Fifth, 5, 2}, {slots.Second, slots.First, 1, 1}});
        CHECK(world.reached(slots.Second) == slots.First);
    }

    {  // leaving every checkpoint empties the group and drops its row
        registry.patch<game::Player>(slots.Lead, [&](game::Player& cactus_value) { cactus_value.x = 100.0F; });
        world.tick();
        CHECK(world.rows() == std::vector<Row>{{slots.Second, slots.First, 1, 1}});
        CHECK_FALSE(registry.all_of<game::Reached>(slots.Lead));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
