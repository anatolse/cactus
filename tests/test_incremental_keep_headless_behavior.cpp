// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#ifndef CACTUS_KEEP_CHECK
#error "this test runs every keep in check mode"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <tuple>
#include <vector>

namespace {

namespace game    = cactus_gen::incremental_keep_runtime;
namespace backend = cactus::runtime::entt_backend;

using OverRow = std::tuple<entt::entity, entt::entity, int>;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
        tick();
    }

    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().incremental_keep_runtime;
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    void step(entt::entity unit, int by) {
        registry.get<game::Unit>(unit).step = by;
    }

    void doom(entt::entity spot) {
        registry.get<game::Spot>(spot).doomed = true;
    }

    std::string& trace() {
        return registry.get<game::Log>(slots().Book).trace;
    }

    // Over rows in store order, with their values.
    std::vector<OverRow> over() {
        std::vector<OverRow> rows;
        for (const auto& entry : backend::relation_store<game::Over>(registry).rows()) {
            rows.emplace_back(entry.from, entry.to, entry.row.depth);
        }
        return rows;
    }

    bool sourced(entt::entity unit) {
        return backend::relation_store<game::Source>(registry).contains(unit, unit);
    }
};

}  // namespace

TEST_CASE("incremental keep: a skipped run changes nothing", "[incremental-keep]") {
    World world;
    const auto& n    = World::slots();
    const auto rows  = world.over();
    REQUIRE(rows == std::vector<OverRow>{{n.C, n.S1, 31}});
    world.trace().clear();
    const auto before = backend::perf_counters();
    for (int frame = 0; frame < 3; ++frame) {
        world.tick();
    }
    CHECK(backend::perf_counters().keep_runs == before.keep_runs);
    CHECK(world.over() == rows);
    CHECK(world.trace().empty());
    CHECK(world.sourced(n.C));
}

TEST_CASE("incremental keep: rows, values and positions follow two moves", "[incremental-keep]") {
    World world;
    const auto& n = World::slots();
    world.step(n.B, -2);
    world.step(n.A, -4);
    world.tick();
    CHECK(world.over() == std::vector<OverRow>{{n.C, n.S1, 31}, {n.B, n.S3, 23}, {n.A, n.S2, 12}});
    world.step(n.C, 2);
    world.step(n.B, -2);
    world.tick();
    CHECK(world.over() == std::vector<OverRow>{{n.A, n.S2, 12}, {n.B, n.S1, 21}, {n.C, n.S3, 33}});
}

TEST_CASE("incremental keep: added traits are delivered oldest first", "[incremental-keep]") {
    World world;
    const auto& n = World::slots();
    world.trace().clear();
    world.step(n.A, -6);
    world.step(n.B, -5);
    world.tick();
    CHECK(world.trace() == "+C2;+C1;");
    CHECK(world.registry.all_of<game::Caught>(n.A));
    CHECK(world.registry.all_of<game::Caught>(n.B));
}

TEST_CASE("incremental keep: a row leaves with its last witness", "[incremental-keep]") {
    World world;
    const auto& n = World::slots();
    REQUIRE(world.sourced(n.C));
    world.doom(n.S1);
    world.tick();
    world.tick();
    CHECK(world.sourced(n.C));
    world.doom(n.S2);
    world.tick();
    world.tick();
    CHECK_FALSE(world.sourced(n.C));
}

TEST_CASE("incremental keep: both witnesses leave in one tick", "[incremental-keep]") {
    World world;
    const auto& n = World::slots();
    world.doom(n.S1);
    world.doom(n.S2);
    // The destroys commit after this tick's keeps; the next tick's keep drops the row.
    world.tick();
    world.tick();
    CHECK_FALSE(world.sourced(n.C));
}

TEST_CASE("incremental keep: host code announces a write with patch", "[incremental-keep]") {
    World world;
    const auto& n = World::slots();
    world.registry.patch<game::Unit>(n.A, [](game::Unit& unit) { unit.x = 3; });
    world.tick();
    CHECK(world.over() == std::vector<OverRow>{{n.C, n.S1, 31}, {n.A, n.S3, 13}});
}

// Leaves the scheduler mid-commit, so it runs last.
TEST_CASE("incremental keep: check mode names a stale keep and its row", "[incremental-keep]") {
    World world;
    const auto& n = World::slots();
    world.registry.get<game::Unit>(n.A).x = 3;
    REQUIRE_THROWS_WITH(world.tick(),
                        Catch::Matchers::ContainsSubstring("incremental_keep_runtime.KeepOver") &&
                            Catch::Matchers::ContainsSubstring("row"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
