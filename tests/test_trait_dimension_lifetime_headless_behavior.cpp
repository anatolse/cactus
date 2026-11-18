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

namespace game    = cactus_gen::trait_dimension_lifetime;
namespace backend = cactus::runtime::entt_backend;

struct World {
    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().trait_dimension_lifetime;
    }

    entt::registry registry;

    World() {
        backend::clear_runtime_faults();
        cactus_headless_test::init_and_load(registry);
    }

    void frame() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] int mark(entt::entity carrier, int x) {
        const auto& board = registry.get<game::Board>(carrier);
        return board.mark.get(board.cactus_shape().at(x));
    }
};

}  // namespace

TEST_CASE("a fresh carrier's cells hold their defaults", "[runtime][trait-dimensions]") {
    World world;
    for (int x = 0; x < 4; ++x) {
        CHECK(world.mark(World::slots().Small, x) == 0);
    }
    CHECK(world.registry.get<game::Board>(World::slots().Wide).cactus_shape().count() == 8);
}

TEST_CASE("a handler writes cells of its own carrier, and outside the range writes nothing", "[runtime][trait-dimensions]") {
    World world;
    world.frame();
    CHECK(world.mark(World::slots().Small, 1) == 7);
    CHECK(world.mark(World::slots().Small, 3) == 5);
    CHECK(world.registry.get<game::Board>(World::slots().Small).mark.stored_count() == 2);
    CHECK(world.mark(World::slots().Wide, 1) == 0);
}

TEST_CASE("add replaces the trait whole, so every cell returns to its default", "[runtime][trait-dimensions]") {
    World world;
    world.frame();
    const auto worn = World::slots().Worn;
    const auto& board = world.registry.get<game::Board>(worn);
    CHECK(board.width == 6);
    CHECK(board.mark.stored_count() == 0);
    CHECK_FALSE(world.registry.all_of<game::Renew>(worn));
    world.frame();
    CHECK(world.mark(worn, 1) == 7);
    CHECK(world.mark(worn, 5) == 5);
}

TEST_CASE("a pair of cells bounded by equalities reads the next cell", "[runtime][trait-dimensions]") {
    World world;
    world.frame();
    const auto& board = world.registry.get<game::Board>(World::slots().Small);
    CHECK(board.echo.get(board.cactus_shape().at(0)) == 7);
    CHECK(board.echo.get(board.cactus_shape().at(2)) == 5);
    CHECK(board.echo.get(board.cactus_shape().at(1)) == 0);
    CHECK(board.echo.get(board.cactus_shape().at(3)) == 0);
}

TEST_CASE("adjacent cells exchange heat inside one carrier, and a frozen carrier is left out",
          "[runtime][trait-dimensions]") {
    World world;
    const auto set = [&](entt::entity carrier, int i, float value) {
        auto& heat = world.registry.get<game::Heat>(carrier);
        heat.value.set(heat.cactus_shape().at(i), value);
    };
    const auto get = [&](entt::entity carrier, int i) {
        const auto& heat = world.registry.get<game::Heat>(carrier);
        return heat.value.get(heat.cactus_shape().at(i));
    };
    set(World::slots().Stove, 3, 1.0F);
    set(World::slots().Icebox, 0, 1.0F);
    world.frame();
    // The last cell has one neighbour, so its group folds one row.
    CHECK(get(World::slots().Stove, 3) == 0.5F);
    CHECK(get(World::slots().Stove, 2) == 0.5F);
    CHECK(get(World::slots().Stove, 0) == 0.0F);
    CHECK(get(World::slots().Icebox, 0) == 1.0F);
    CHECK(get(World::slots().Icebox, 1) == 0.0F);
}

TEST_CASE("a keep that reads its own cells follows the written carrier", "[runtime][trait-dimensions]") {
    World world;
    world.frame();
    CHECK(world.registry.all_of<game::Marked>(World::slots().Small));
    CHECK_FALSE(world.registry.all_of<game::Marked>(World::slots().Wide));
}

TEST_CASE("a keep that reads a single trait's cells runs only after a write", "[runtime][trait-dimensions]") {
    World world;
    const auto runs = [&] {
        const auto before = backend::perf_counters().keep_runs;
        world.frame();
        return backend::perf_counters().keep_runs - before;
    };
    const auto first = runs();
    CHECK(world.registry.all_of<game::Alert>(World::slots().Small));
    world.frame();
    const auto quiet = runs();
    CHECK(quiet < first);
    CHECK(world.registry.all_of<game::Alert>(World::slots().Small));
}

TEST_CASE("an added trigger of a dimensioned trait runs once for the carrier", "[runtime][trait-dimensions]") {
    World world;
    world.frame();
    world.frame();
    CHECK(world.registry.get<game::Born>(World::slots().Stove).times == 1);
}

TEST_CASE("an empty range gives no cells", "[runtime][trait-dimensions]") {
    World world;
    const auto& board = world.registry.get<game::Board>(World::slots().Empty);
    CHECK(board.cactus_shape().count() == 0);
    CHECK(board.mark.get(board.cactus_shape().at(0)) == 0);
}

TEST_CASE("a range over the limit faults when the value is made", "[runtime][trait-dimensions]") {
    World world;
    CHECK_FALSE(world.registry.valid(World::slots().Huge));
    bool found = false;
    for (const auto& fault : backend::runtime_faults()) {
        found = found || (fault.reason == backend::RangeFaultReason::DimensionTooLarge && fault.field == "x");
    }
    CHECK(found);
}

TEST_CASE("a restore brings cells back at their defaults", "[runtime][trait-dimensions]") {
    World world;
    world.frame();
    CHECK(world.mark(World::slots().Small, 1) == 7);
    const auto snapshot = backend::generated_capture_world_snapshot(world.registry);
    const auto restored = backend::generated_restore_world(world.registry, snapshot);
    INFO(restored.code << ": " << restored.message);
    REQUIRE(restored.ok);
    const auto view = world.registry.view<game::Board, game::Fill>();
    REQUIRE(view.size_hint() == 1);
    const auto& board = world.registry.get<game::Board>(*view.begin());
    CHECK(board.mark.stored_count() == 0);
    CHECK(board.width == 4);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
