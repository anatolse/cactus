// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {

namespace game = cactus_gen::neighbor_joins;

constexpr float kDt = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    [[nodiscard]] static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().neighbor_joins;
    }
    int ticks() {
        return registry.get<game::Ticks>(slots().Clock).count;
    }
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            const int before = ticks();
            cactus_headless_test::drive_frame(registry, kDt);
            REQUIRE(ticks() == before + 1);
        }
    }
    game::Cell& cell(entt::entity entity) {
        return registry.get<game::Cell>(entity);
    }
    // The reports of one tick, from a cleared log.
    game::Log report() {
        registry.get<game::Log>(slots().Book) = game::Log{};
        tick();
        return registry.get<game::Log>(slots().Book);
    }
    // What the full product of cells reports, in creation order.
    game::Log expected_report() {
        std::vector<std::pair<std::uint64_t, entt::entity>> cells;
        for (const auto entity : registry.view<game::Cell>()) {
            cells.emplace_back(registry.get<cactus::runtime::entt_backend::CreationOrdinal>(entity).value, entity);
        }
        std::ranges::sort(cells);
        game::Log log;
        for (const auto& [left_ordinal, a] : cells) {
            for (const auto& [right_ordinal, b] : cells) {
                const auto& from = cell(a);
                const auto& to   = cell(b);
                if (std::abs(from.x - to.x) + std::abs(from.y - to.y) != 1) {
                    continue;
                }
                log.count += 1;
                log.trace = (log.trace * 31 + from.id * 9 + to.id) % 1000003;
            }
        }
        return log;
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("neighbor joins: four-neighbor rules on a grid", "[runtime][neighbor-joins]") {
    World world;
    auto& registry    = world.registry;
    const auto& slots = World::slots();

    world.tick();

    {  // every directed neighbor pair reports once, in full-product order
        const auto expected = world.expected_report();
        REQUIRE(expected.count == 24);
        const auto log = world.report();
        CHECK(log.count == expected.count);
        CHECK(log.trace == expected.trace);
    }

    {  // corners have two neighbors, edges three, the center four
        CHECK(world.cell(slots.Corner0).neighbors == 2);
        CHECK(world.cell(slots.Corner8).neighbors == 2);
        CHECK(world.cell(slots.Edge1).neighbors == 3);
        CHECK(world.cell(slots.Edge5).neighbors == 3);
        CHECK(world.cell(slots.Center).neighbors == 4);
    }

    {  // `order by:` + `limit: 1 per a` picks the neighbor with the highest id
        CHECK(world.cell(slots.Center).best == 7);
        CHECK(world.cell(slots.Corner0).best == 3);
        CHECK(world.cell(slots.Edge1).best == 4);
        CHECK(world.cell(slots.Corner2).best == 5);
        CHECK(world.cell(slots.Edge3).best == 6);
        CHECK(world.cell(slots.Edge5).best == 8);
        CHECK(world.cell(slots.Corner6).best == 7);
        CHECK(world.cell(slots.Edge7).best == 8);
        CHECK(world.cell(slots.Corner8).best == 7);
    }

    {  // a cell both branches find counts once
        CHECK(world.cell(slots.Corner0).ahead == 1);
        CHECK(world.cell(slots.Center).ahead == 1);
        CHECK(world.cell(slots.Corner2).ahead == 0);
        world.cell(slots.Corner0).reach = 2;
        world.tick();
        CHECK(world.cell(slots.Corner0).ahead == 2);
        world.cell(slots.Corner0).reach = 1;
        world.tick();
        CHECK(world.cell(slots.Corner0).ahead == 1);
    }

    {  // cells next to an open cell are kept as they open and close
        CHECK(registry.view<game::NextToOpen>().empty());
        world.cell(slots.Center).open = true;
        world.tick();
        for (const auto entity : {slots.Edge1, slots.Edge3, slots.Edge5, slots.Edge7}) {
            CHECK(registry.all_of<game::NextToOpen>(entity));
        }
        CHECK_FALSE(registry.all_of<game::NextToOpen>(slots.Center));
        CHECK_FALSE(registry.all_of<game::NextToOpen>(slots.Corner0));

        world.cell(slots.Center).open = false;
        world.cell(slots.Corner8).open = true;
        world.tick();
        CHECK(registry.all_of<game::NextToOpen>(slots.Edge5));
        CHECK(registry.all_of<game::NextToOpen>(slots.Edge7));
        CHECK_FALSE(registry.all_of<game::NextToOpen>(slots.Edge1));
        CHECK_FALSE(registry.all_of<game::NextToOpen>(slots.Edge3));
        world.cell(slots.Corner8).open = false;
        world.tick();
        CHECK(registry.view<game::NextToOpen>().empty());
    }

    {  // a cell leaving and rejoining the grid
        const auto center = world.cell(slots.Center);
        registry.remove<game::Cell>(slots.Center);
        auto expected = world.expected_report();
        REQUIRE(expected.count == 16);
        auto log = world.report();
        CHECK(log.count == expected.count);
        CHECK(log.trace == expected.trace);
        CHECK(world.cell(slots.Edge1).neighbors == 2);
        CHECK(world.cell(slots.Edge1).best == 2);

        registry.emplace<game::Cell>(slots.Center, center);
        expected = world.expected_report();
        log      = world.report();
        CHECK(log.count == 24);
        CHECK(log.trace == expected.trace);
        CHECK(world.cell(slots.Edge1).neighbors == 3);
        CHECK(world.cell(slots.Edge1).best == 4);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
