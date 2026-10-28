#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

namespace game    = cactus_gen::relation_endpoint_limits;
namespace backend = cactus::runtime::entt_backend;

TEST_CASE("Endpoint capacities greedily select scored rows", "[headless][endpoint-limit]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);
    const auto& slots  = cactus_gen::detail::generated_named_slots().relation_endpoint_limits;
    const auto& claims = backend::relation_store<game::Claim>(registry);
    CHECK(claims.contains(slots.First, slots.Middle));
    CHECK(claims.contains(slots.Second, slots.Finish));
    CHECK_FALSE(claims.contains(slots.Second, slots.Middle));
}

TEST_CASE("Endpoint route allocation prefers the nearer robot", "[headless][endpoint-limit]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots = cactus_gen::detail::generated_named_slots().relation_endpoint_limits;
    registry.get<game::Control>(slots.Settings).mode = 1;
    cactus_headless_test::drive_frame(registry);
    const auto& claims = backend::relation_store<game::Claim>(registry);
    CHECK(claims.contains(slots.Second, slots.Finish));
    CHECK_FALSE(claims.contains(slots.First, slots.Finish));
}

TEST_CASE("Mixed entity and endpoint capacities select independently", "[headless][endpoint-limit]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots = cactus_gen::detail::generated_named_slots().relation_endpoint_limits;
    registry.get<game::Control>(slots.Settings).mode = 2;
    cactus_headless_test::drive_frame(registry);
    CHECK(registry.get<game::Control>(slots.Settings).hits == 2);
}
namespace {
struct World {
    entt::registry registry;
    World() {
        cactus_headless_test::init_and_load(registry);
    }
    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().relation_endpoint_limits;
    }
    game::Control& control() {
        return registry.get<game::Control>(slots().Settings);
    }
    auto& candidates() {
        return backend::relation_store<game::Candidate>(registry);
    }
    std::string run(int mode) {
        control().mode = mode;
        control().trace.clear();
        control().hits = 0;
        cactus_headless_test::drive_frame(registry);
        return control().trace;
    }
};

struct Candidate {
    entt::entity from;
    entt::entity to;
    int score;
    int rank;
};

std::string reference(std::vector<Candidate> rows, int from_cap, int to_cap) {
    std::ranges::stable_sort(rows, {}, &Candidate::rank);
    std::vector<Candidate> selected;
    std::string trace;
    for (const auto& row : rows) {
        const auto from_count = std::ranges::count(selected, row.from, &Candidate::from);
        const auto to_count   = std::ranges::count(selected, row.to, &Candidate::to);
        if (from_count >= from_cap || to_count >= to_cap) {
            continue;
        }
        selected.push_back(row);
        trace += std::to_string(row.score) + ";";
    }
    return trace;
}
}  // namespace

TEST_CASE("Endpoint rows agree with an independent greedy selector", "[headless][endpoint-limit]") {
    World world;
    std::vector<entt::entity> robots;
    std::vector<entt::entity> tiles;
    for (int index = 0; index < 5; ++index) {
        const auto robot = backend::create_entity(world.registry);
        world.registry.emplace<game::Robot>(robot);
        robots.push_back(robot);
        const auto tile = backend::create_entity(world.registry);
        world.registry.emplace<game::Tile>(tile);
        tiles.push_back(tile);
    }
    for (int seed = 0; seed < 12; ++seed) {
        world.candidates().clear();
        world.candidates().compact();
        std::vector<Candidate> rows;
        for (std::size_t from = 0; from < robots.size(); ++from) {
            for (std::size_t to = 0; to < tiles.size(); ++to) {
                const auto score = static_cast<int>(from * tiles.size() + to) + 1;
                if ((score + seed) % 3 == 0) {
                    continue;
                }
                const auto rank = (score * 7 + seed) % 5;
                rows.push_back({robots[from], tiles[to], score, rank});
            }
        }
        std::ranges::reverse(rows);
        for (const auto& row : rows) {
            world.candidates().add(row.from, row.to, game::Candidate{.score = row.score, .rank = row.rank});
        }
        INFO(seed);
        CHECK(world.run(3) == reference(rows, 2, 2));
        CHECK(world.run(4) == reference(rows, 1, 100));
    }
}

TEST_CASE("Endpoint ties use row order and each activation starts fresh", "[headless][endpoint-limit]") {
    World world;
    const auto& slots = World::slots();
    world.candidates().clear();
    world.candidates().compact();
    world.candidates().add(slots.Second, slots.Finish, game::Candidate{.score = 8});
    world.candidates().add(slots.Second, slots.Middle, game::Candidate{.score = 9});
    world.candidates().add(slots.First, slots.Middle, game::Candidate{.score = 10});
    CHECK(world.run(4) == "8;10;");
    CHECK(world.run(4) == "8;10;");
    CHECK(world.run(5).empty());
    CHECK(world.control().hits == 0);
    world.candidates().clear();
    world.candidates().compact();
    CHECK(world.run(3).empty());
}

TEST_CASE("Greedy allocation skips without reserving and need not maximize assignments", "[headless][endpoint-limit]") {
    World world;
    const auto& slots = World::slots();
    world.candidates().erase(slots.Second, slots.Finish);
    world.run(0);
    auto& claims = backend::relation_store<game::Claim>(world.registry);
    CHECK(claims.size() == 1);
    CHECK(claims.contains(slots.First, slots.Middle));
    claims.clear();
    claims.compact();
    world.candidates().add(slots.Second, slots.Finish, game::Candidate{.score = 4});
    world.run(0);
    CHECK(claims.size() == 2);
    CHECK(claims.contains(slots.Second, slots.Finish));
    world.run(0);
    CHECK(claims.size() == 2);
}

TEST_CASE("Endpoint early return consumes capacity and queued destruction preserves snapshot",
          "[headless][endpoint-limit]") {
    World world;
    CHECK(world.run(6) == "4;");
    CHECK(world.run(7) == "1;4;");
    CHECK_FALSE(world.registry.valid(World::slots().Finish));
    CHECK(world.run(7) == "999;");
}

TEST_CASE("Endpoint counters are not saved and restore recomputes selection", "[headless][endpoint-limit]") {
    World world;
    CHECK(world.run(3) == "1;2;3;4;");
    const auto snapshot = backend::generated_capture_world_snapshot(world.registry);
    world.candidates().clear();
    world.candidates().compact();
    CHECK(world.run(3).empty());
    const auto restored = backend::generated_restore_world(world.registry, snapshot);
    REQUIRE(restored.ok);
    CHECK(world.run(3) == "1;2;3;4;");
}

TEST_CASE("Both route join orders reuse the bounded route cache", "[headless][endpoint-limit]") {
    World world;
    world.run(8);
    CHECK(world.control().hits == 2);
    auto& routes      = backend::derived_relation<game::Route>(world.registry);
    const auto builds = routes.builds();
    world.run(9);
    CHECK(world.control().hits == 2);
    CHECK(routes.builds() == builds);
    CHECK(routes.cached_keys() <= 3);
}

TEST_CASE("A robot already on a goal claims through the separate arrival rule", "[headless][endpoint-limit]") {
    World world;
    const auto& slots = World::slots();
    auto& places      = backend::relation_store<game::Located>(world.registry);
    places.erase_from(slots.First);
    places.add(slots.First, slots.Finish, game::Located{});
    world.run(10);
    CHECK(backend::relation_store<game::Claim>(world.registry).contains(slots.First, slots.Finish));
}
TEST_CASE("Each witness consumes capacity without deduplication", "[headless][endpoint-limit]") {
    World world;
    CHECK(world.run(11) == "1;1;4;4;");
    CHECK(world.run(13).empty());
    CHECK(world.control().hits == 0);
}

TEST_CASE("Endpoint counter roles remain separate for equal entity handles", "[headless][endpoint-limit]") {
    World world;
    const auto& slots = World::slots();
    auto& edges       = backend::relation_store<game::Walk>(world.registry);
    edges.clear();
    edges.compact();
    edges.add(slots.Start, slots.Middle, game::Walk{.cost = 1});
    edges.add(slots.Middle, slots.Start, game::Walk{.cost = 2});
    CHECK(world.run(12) == "1;2;");
}

TEST_CASE("Endpoint allocation workload measurements", "[.endpoint-benchmark]") {
    for (const int size : {16, 32, 64}) {
        World world;
        auto& edges  = backend::relation_store<game::Walk>(world.registry);
        auto& places = backend::relation_store<game::Located>(world.registry);
        edges.clear();
        edges.compact();
        places.clear();
        places.compact();
        world.candidates().clear();
        world.candidates().compact();
        std::vector<entt::entity> tiles;
        std::vector<entt::entity> robots;
        for (int index = 0; index < size; ++index) {
            const auto tile = backend::create_entity(world.registry);
            world.registry.emplace<game::Tile>(tile);
            world.registry.emplace<backend::CreationOrdinal>(tile, backend::generated_next_creation_ordinal());
            tiles.push_back(tile);
            const auto robot = backend::create_entity(world.registry);
            world.registry.emplace<game::Robot>(robot);
            world.registry.emplace<backend::CreationOrdinal>(robot, backend::generated_next_creation_ordinal());
            robots.push_back(robot);
            places.add(robot, tile, game::Located{});
            if (tiles.size() > 1) {
                edges.add(tiles[tiles.size() - 2], tile, game::Walk{});
            }
        }
        auto& routes           = backend::derived_relation<game::Route>(world.registry);
        using Clock            = std::chrono::steady_clock;
        const auto query_start = Clock::now();
        std::size_t candidates = 0;
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            routes.for_each_from(tiles[index], [&](const auto& route) {
                ++candidates;
                world.candidates().add(
                    robots[index],
                    route.to,
                    game::Candidate{.score = static_cast<int>(candidates), .rank = route.row.distance});
            });
        }
        const auto query_end = Clock::now();
        world.run(3);
        const auto allocation_end = Clock::now();
        const auto builds         = routes.builds();
        world.run(8);
        CHECK(routes.builds() == builds);
        CHECK(candidates == static_cast<std::size_t>(size * (size - 1) / 2));
        const auto query_us = std::chrono::duration_cast<std::chrono::microseconds>(query_end - query_start).count();
        const auto allocation_us =
            std::chrono::duration_cast<std::chrono::microseconds>(allocation_end - query_end).count();
        std::cout << "endpoint-benchmark nodes=" << size << " candidates=" << candidates << " query_us=" << query_us
                  << " allocation_frame_us=" << allocation_us
                  << " candidate_payload_bytes=" << candidates * sizeof(backend::RelationStore<game::Candidate>::Entry)
                  << " cache_builds=" << builds << '\n';
    }
}

TEST_CASE("Authored join order determines the stable selected sequence", "[headless][endpoint-limit]") {
    World world;
    const auto& slots = World::slots();
    auto& places      = backend::relation_store<game::Located>(world.registry);
    places.clear();
    places.compact();
    places.add(slots.Second, slots.Middle, game::Located{});
    places.add(slots.First, slots.Start, game::Located{});
    auto& selected = backend::relation_store<game::Selected>(world.registry);
    world.run(8);
    REQUIRE(selected.rows().size() == 2);
    CHECK(selected.rows()[0].from == slots.Second);
    CHECK(selected.rows()[1].from == slots.First);
    selected.clear();
    selected.compact();
    world.run(9);
    REQUIRE(selected.rows().size() == 2);
    CHECK(selected.rows()[0].from == slots.First);
    CHECK(selected.rows()[1].from == slots.Second);
}