// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <vector>

namespace {

namespace game = cactus_gen::trait_dimensions;
using Catch::Matchers::WithinAbs;

struct World {
    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().trait_dimensions;
    }

    entt::registry registry;

    World() {
        cactus::runtime::entt_backend::clear_runtime_faults();
        cactus_headless_test::init_and_load(registry);
    }

    void frames(int count) {
        for (int frame = 0; frame < count; ++frame) {
            cactus_headless_test::drive_frame(registry);
        }
    }

    [[nodiscard]] const game::Room& room() {
        return registry.get<game::Room>(slots().Hall);
    }
    [[nodiscard]] float gas(int x, int y) {
        const auto& table = room();
        return table.gas.get(table.cactus_shape().at(x, y));
    }
    [[nodiscard]] int count(entt::entity carrier, game::Item item) {
        const auto& table = registry.get<game::Stock>(carrier);
        return table.count.get(table.cactus_shape().at(static_cast<int>(item)));
    }

};

}  // namespace

TEST_CASE("a fresh carrier reads every cell at its default", "[runtime][trait-dimensions]") {
    World world;
    CHECK(world.gas(2, 3) == 0.0F);
    CHECK(world.count(World::slots().Pack, game::Item::Key) == 0);
    CHECK(world.room().gas.stored_count() == 0);
}

TEST_CASE("one spread step moves a tenth of the difference to each neighbour", "[runtime][trait-dimensions]") {
    World world;
    world.frames(1);
    CHECK_THAT(world.gas(2, 3), WithinAbs(0.6, 1e-6));
    for (const auto [x, y] : {std::pair{1, 3}, std::pair{3, 3}, std::pair{2, 2}, std::pair{2, 4}}) {
        CHECK_THAT(world.gas(x, y), WithinAbs(0.1, 1e-6));
    }
    CHECK(world.gas(4, 3) == 0.0F);
    CHECK_FALSE(world.registry.valid(World::slots().Pin));
}

TEST_CASE("two unordered writers of one cell add exactly", "[runtime][trait-dimensions]") {
    World world;
    world.frames(1);
    const float source = 0.25F + 0.5F;
    const float out    = (0.0F - source) * 0.1F;
    CHECK(world.gas(12, 8) == source + (4.0F * out));
}

TEST_CASE("access outside a range changes nothing and reports no fault", "[runtime][trait-dimensions]") {
    World world;
    world.frames(1);
    CHECK(cactus::runtime::entt_backend::runtime_fault_count() == 0);
    CHECK_FALSE(world.registry.valid(World::slots().Stray));
}

TEST_CASE("walls keep gas out", "[runtime][trait-dimensions]") {
    World world;
    world.frames(40);
    for (int y = 0; y < 12; ++y) {
        CHECK(world.gas(6, y) == 0.0F);
    }
    CHECK(world.gas(11, 8) > 0.0F);
}

TEST_CASE("a cell written back to its default is no longer stored", "[runtime][trait-dimensions]") {
    World world;
    world.frames(1);
    CHECK(world.gas(2, 3) > 0.0F);
    world.frames(600);
    std::vector<std::uint64_t> cells;
    world.room().gas.append_stored(cells);
    const auto shape = world.room().cactus_shape();
    for (const auto cell : cells) {
        CHECK(shape.key(cell, 0) > 6);
    }
}

TEST_CASE("cells of two carriers stay apart", "[runtime][trait-dimensions]") {
    World world;
    world.frames(1);
    CHECK(world.count(World::slots().Shop, game::Item::Key) == 1);
    CHECK(world.registry.get<game::Tally>(World::slots().Shop).total == 3);
    world.frames(4);
    CHECK(world.count(World::slots().Shop, game::Item::Rope) == 3);
    CHECK(world.registry.get<game::Tally>(World::slots().Shop).total == 9);
    CHECK(world.count(World::slots().Pack, game::Item::Rope) == 0);
    CHECK(world.count(World::slots().Pack, game::Item::Key) == 5);
    CHECK(world.registry.get<game::Tally>(World::slots().Pack).total == 5);
}

TEST_CASE("a guard's dose grows only in gas", "[runtime][trait-dimensions]") {
    World world;
    world.frames(1);
    CHECK_THAT(world.registry.get<game::Guard>(World::slots().Watch).dose, WithinAbs(0.1, 1e-6));
    world.frames(20);
    CHECK(world.registry.get<game::Guard>(World::slots().Post).dose == 0.0F);
    CHECK(world.registry.get<game::Guard>(World::slots().Sniff).dose > 1.0F);
    CHECK(world.registry.all_of<game::InGas>(World::slots().Sniff));
    CHECK_FALSE(world.registry.all_of<game::InGas>(World::slots().Post));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
