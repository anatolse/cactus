#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <utility>
#include <vector>

namespace {
namespace game    = cactus_gen::relation_row_projection;
namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;
    World() {
        cactus_headless_test::init_and_load(registry);
    }
    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().relation_row_projection;
    }
    template <typename Row>
    auto& rows() {
        return backend::relation_store<Row>(registry);
    }
    void tick() {
        cactus_headless_test::drive_frame(registry);
    }
};

using Pair = std::pair<entt::entity, entt::entity>;

template <typename Row>
std::vector<Pair> pairs(const backend::RelationStore<Row>& store) {
    std::vector<Pair> result;
    for (const auto& entry : store.rows()) {
        result.emplace_back(entry.from, entry.to);
    }
    return result;
}

std::vector<Pair> reference(World& world, std::vector<Pair> previous = {}) {
    std::vector<Pair> supported;
    for (const auto& owner : world.rows<game::OwnedBy>().rows()) {
        for (const auto& edge : world.rows<game::Walk>().rows()) {
            const Pair pair{owner.to, edge.to};
            if (owner.from == edge.from && !world.rows<game::OwnedBy>().has_from(edge.to) &&
                !std::ranges::contains(supported, pair)) {
                supported.push_back(pair);
            }
        }
    }
    std::erase_if(previous, [&](const Pair& pair) { return !std::ranges::contains(supported, pair); });
    for (const auto& pair : supported) {
        if (!std::ranges::contains(previous, pair)) {
            previous.push_back(pair);
        }
    }
    return previous;
}

TEST_CASE("Kept row projections maintain frontier and direct keys", "[headless][row-projection]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto& slots = cactus_gen::detail::generated_named_slots().relation_row_projection;
    auto& owners      = backend::relation_store<game::OwnedBy>(registry);
    auto& walk        = backend::relation_store<game::Walk>(registry);
    owners.add(slots.Start, slots.Red, {});
    walk.add(slots.Start, slots.Goal, {});
    cactus_headless_test::drive_frame(registry);
    CHECK(backend::relation_store<game::Frontier>(registry).contains(slots.Red, slots.Goal));
    CHECK(backend::relation_store<game::Reverse>(registry).contains(slots.Goal, slots.Start));
    CHECK(backend::relation_store<game::Sources>(registry).contains(slots.Start, slots.Start));
    CHECK(backend::relation_store<game::Mixed>(registry).contains(slots.Start, slots.Goal));
}

TEST_CASE("Duplicate support preserves membership and creation order", "[headless][row-projection]") {
    World world;
    const auto& s = World::slots();
    auto& owners  = world.rows<game::OwnedBy>();
    auto& walk    = world.rows<game::Walk>();
    owners.add(s.Start, s.Red, {});
    owners.add(s.Other, s.Red, {});
    walk.add(s.Start, s.Goal, {});
    walk.add(s.Start, s.Last, {});
    walk.add(s.Other, s.Goal, {});
    auto expected = reference(world);
    world.tick();
    CHECK(pairs(world.rows<game::Frontier>()) == expected);
    REQUIRE(expected.size() == 2);
    const auto* row = world.rows<game::Frontier>().find(s.Red, s.Goal);
    REQUIRE(row != nullptr);
    CHECK(row->cost == 2);
    CHECK(row->priority == 7);
    const auto revision = world.rows<game::Frontier>().revision();
    world.tick();
    CHECK(world.rows<game::Frontier>().revision() == revision);
    world.registry.get<game::Control>(s.Settings).cost = 9;
    world.tick();
    const auto* updated = world.rows<game::Frontier>().find(s.Red, s.Goal);
    REQUIRE(updated != nullptr);
    CHECK(updated->priority == 9);
    CHECK(pairs(world.rows<game::Frontier>()) == expected);
    walk.erase(s.Start, s.Goal);
    walk.compact();
    expected = reference(world, expected);
    world.tick();
    CHECK(pairs(world.rows<game::Frontier>()) == expected);
    REQUIRE(world.rows<game::Frontier>().contains(s.Red, s.Goal));
    owners.clear();
    owners.compact();
    owners.add(s.Other, s.Red, {});
    owners.add(s.Start, s.Red, {});
    expected = reference(world, expected);
    world.tick();
    CHECK(pairs(world.rows<game::Frontier>()) == expected);
    walk.erase(s.Other, s.Goal);
    walk.compact();
    expected = reference(world, expected);
    world.tick();
    CHECK(pairs(world.rows<game::Frontier>()) == expected);
    CHECK_FALSE(world.rows<game::Frontier>().contains(s.Red, s.Goal));
    walk.add(s.Other, s.Goal, {});
    expected = reference(world, expected);
    world.tick();
    CHECK(pairs(world.rows<game::Frontier>()) == expected);
    CHECK(expected == std::vector<Pair>{{s.Red, s.Last}, {s.Red, s.Goal}});
    owners.add(s.Other, s.Blue, {});
    expected = reference(world, expected);
    world.tick();
    CHECK(pairs(world.rows<game::Frontier>()) == expected);
    owners.add(s.Goal, s.Blue, {});
    expected = reference(world, expected);
    world.tick();
    CHECK(pairs(world.rows<game::Frontier>()) == expected);
    walk.clear();
    walk.compact();
    world.tick();
    CHECK(world.rows<game::Frontier>().size() == 0);
    CHECK(world.rows<game::Sources>().size() == 0);
}

TEST_CASE("Paused projections and restore rebuild only on a scheduled run", "[headless][row-projection]") {
    World world;
    const auto& s = World::slots();
    world.rows<game::OwnedBy>().add(s.Start, s.Red, {});
    world.rows<game::Walk>().add(s.Start, s.Goal, {});
    world.tick();
    REQUIRE(world.rows<game::Frontier>().contains(s.Red, s.Goal));
    const auto snapshot = backend::generated_capture_world_snapshot(world.registry);
    for (const auto& row : snapshot.relation_rows()) {
        CHECK(snapshot.relation_name(row) != "relation_row_projection.Frontier");
        CHECK(snapshot.relation_name(row) != "relation_row_projection.Reverse");
    }
    world.registry.get<game::Control>(s.Settings).active = false;
    world.registry.get<game::Control>(s.Settings).cost   = 42;
    world.rows<game::Walk>().clear();
    world.rows<game::Walk>().compact();
    world.tick();
    CHECK(world.rows<game::Frontier>().contains(s.Red, s.Goal));
    const auto* paused = world.rows<game::Frontier>().find(s.Red, s.Goal);
    REQUIRE(paused != nullptr);
    CHECK(paused->priority == 7);
    world.registry.get<game::Control>(s.Settings).active = true;
    CHECK(world.rows<game::Frontier>().contains(s.Red, s.Goal));
    world.tick();
    CHECK(world.rows<game::Frontier>().size() == 0);
    REQUIRE(backend::generated_restore_world(world.registry, snapshot).ok);
    CHECK(world.rows<game::Frontier>().size() == 0);
    world.tick();
    CHECK(world.rows<game::Frontier>().contains(s.Red, s.Goal));
    world.tick();
    CHECK(world.rows<game::Frontier>().size() == 1);
}
TEST_CASE("Projection retains authored order with reversed and derived lookup plans", "[headless][row-projection]") {
    World world;
    const auto& s = World::slots();
    auto& owners  = world.rows<game::OwnedBy>();
    auto& walk    = world.rows<game::Walk>();
    owners.add(s.Other, s.Blue, {});
    owners.add(s.Start, s.Red, {});
    walk.add(s.Start, s.Goal, {});
    walk.add(s.Other, s.Last, {});
    walk.add(s.Goal, s.Last, {});
    world.tick();
    CHECK(pairs(world.rows<game::Frontier>()) == std::vector<Pair>{{s.Blue, s.Last}, {s.Red, s.Goal}});
    CHECK(pairs(world.rows<game::ReverseFrontier>()) == std::vector<Pair>{{s.Red, s.Goal}, {s.Blue, s.Last}});
    CHECK(world.rows<game::Reachable>().contains(s.Start, s.Last));
    CHECK(world.rows<game::ReverseReachable>().contains(s.Start, s.Last));
    CHECK(world.rows<game::Copied>().size() == 0);
    world.tick();
    CHECK(pairs(world.rows<game::Copied>()) == pairs(world.rows<game::Reverse>()));
    CHECK(backend::derived_relation<game::Reach>(world.registry).cached_keys() <= 4);
}

TEST_CASE("A phase with no activation leaves a projection untouched", "[headless][row-projection]") {
    World world;
    const auto& s = World::slots();
    world.rows<game::Walk>().add(s.Start, s.Goal, {});
    cactus_headless_test::drive_frame(world.registry, 0.1F);
    CHECK(world.rows<game::SlowCopies>().size() == 0);
    cactus_headless_test::drive_frame(world.registry, 0.5F);
    CHECK(world.rows<game::SlowCopies>().contains(s.Start, s.Goal));
    world.rows<game::Walk>().clear();
    world.rows<game::Walk>().compact();
    cactus_headless_test::drive_frame(world.registry, 0.01F);
    CHECK(world.rows<game::SlowCopies>().contains(s.Start, s.Goal));
    cactus_headless_test::drive_frame(world.registry, 0.5F);
    CHECK(world.rows<game::SlowCopies>().size() == 0);
}

TEST_CASE("Right-driven derived projection agrees with a product reference", "[headless][row-projection]") {
    World world;
    const auto& s = World::slots();
    const std::array nodes{s.Start, s.Other, s.Goal, s.Last};
    const std::array edges{Pair{s.Start, s.Goal}, Pair{s.Last, s.Other}, Pair{s.Other, s.Last}, Pair{s.Goal, s.Last}};
    std::array<std::array<bool, 4>, 4> reachable{};
    for (const auto& [from, to] : edges) {
        world.rows<game::Walk>().add(from, to, {});
        const auto i    = static_cast<std::size_t>(std::ranges::find(nodes, from) - nodes.begin());
        const auto j    = static_cast<std::size_t>(std::ranges::find(nodes, to) - nodes.begin());
        reachable[i][j] = true;
    }
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            for (std::size_t j = 0; j < nodes.size(); ++j) {
                reachable[i][j] = reachable[i][j] || (reachable[i][k] && reachable[k][j]);
            }
        }
    }
    std::vector<Pair> expected;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (std::size_t j = 0; j < nodes.size(); ++j) {
            for (const auto& [from, to] : edges) {
                const Pair pair{from, nodes[j]};
                if (reachable[i][j] && to == nodes[i] && !std::ranges::contains(expected, pair)) {
                    expected.push_back(pair);
                }
            }
        }
    }
    world.tick();
    CHECK(pairs(world.rows<game::ReverseReachable>()) == expected);
    world.tick();
    CHECK(pairs(world.rows<game::ReverseReachable>()) == expected);
}
TEST_CASE("Projected rows cannot outlive an endpoint retired earlier in the commit", "[headless][row-projection]") {
    World world;
    const auto& s = World::slots();
    world.rows<game::OwnedBy>().add(s.Start, s.Red, {});
    world.rows<game::Walk>().add(s.Start, s.Goal, {});
    world.registry.get<game::Control>(s.Settings).retire = true;
    SECTION("destroy endpoint") {}
    SECTION("remove endpoint trait") {
        world.registry.get<game::Control>(s.Settings).strip = true;
    }
    world.tick();
    CHECK_FALSE(world.rows<game::Frontier>().contains(s.Red, s.Goal));
    CHECK_FALSE(world.rows<game::Reverse>().contains(s.Goal, s.Start));
    CHECK_FALSE(world.rows<game::Mixed>().contains(s.Start, s.Goal));
}

void measure_projection(std::size_t source_count, std::size_t fanout) {
    World world;
    const auto create_tile = [&] {
        const auto entity = backend::create_entity(world.registry);
        world.registry.emplace<backend::CreationOrdinal>(entity, backend::generated_next_creation_ordinal());
        world.registry.emplace<game::Tile>(entity);
        return entity;
    };
    std::vector<entt::entity> destinations;
    for (std::size_t index = 0; index < fanout; ++index) {
        destinations.push_back(create_tile());
    }
    for (std::size_t index = 0; index < source_count; ++index) {
        const auto source = create_tile();
        world.rows<game::OwnedBy>().add(source, World::slots().Red, {});
        for (const auto target : destinations) {
            world.rows<game::Walk>().add(source, target, {});
        }
    }
    const auto start = std::chrono::steady_clock::now();
    world.tick();
    const auto cold = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::vector<double> times;
    for (int run = 0; run < 5; ++run) {
        const auto before = std::chrono::steady_clock::now();
        world.tick();
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count());
    }
    std::ranges::sort(times);
    CHECK(world.rows<game::Frontier>().size() == fanout);
    std::cout << "sources=" << source_count << " walk=" << source_count * fanout << " matches=" << source_count * fanout
              << " unique=" << fanout << " cold_ms=" << cold << " warm_median_ms=" << times[2] << '\n';
}

TEST_CASE("Projection benchmark 64x8", "[.projection-benchmark]") {
    measure_projection(64, 8);
}
TEST_CASE("Projection benchmark 256x8", "[.projection-benchmark]") {
    measure_projection(256, 8);
}
TEST_CASE("Projection benchmark 256x32", "[.projection-benchmark]") {
    measure_projection(256, 32);
}
TEST_CASE("Projection benchmark 1024x32", "[.projection-benchmark]") {
    measure_projection(1024, 32);
}
}  // namespace
