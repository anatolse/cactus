// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

using Item      = creation_order_iteration_runtime__Item;
using Offspring = creation_order_iteration_runtime__Offspring;
using Log       = creation_order_iteration_runtime__Log;
using cactus::runtime::entt_backend::CreationOrdinal;

// Items A..D get ids 1..4. A leaves before D arrives, so EnTT's swap-and-pop
// leaves the pool as C, B, D: storage order is not creation order.
struct World {
    entt::registry registry;

    World() {
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);
        arrive(1);
        arrive(2);
        arrive(3);
        inject(creation_order_iteration_runtime__ItemLeftEvent{.id = 1});
        arrive(4);
    }

    void inject(auto occurrence) {
        cactus::runtime::entt_backend::generated_inject_external_event(occurrence);
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
    }

    void arrive(int id) {
        inject(creation_order_iteration_runtime__ItemArrivedEvent{.id = id});
    }

    const Log& run_frame() {
        cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
        const auto view = registry.view<Log>();
        REQUIRE(view.begin() != view.end());
        return registry.get<Log>(*view.begin());
    }

    std::vector<int> picked_ids() {
        std::vector<int> ids;
        for (auto [entity, item] : registry.view<Item>().each()) {
            if (item.picked) {
                ids.push_back(item.id);
            }
        }
        std::ranges::sort(ids);
        return ids;
    }

    std::vector<int> offspring_origins_in_creation_order() {
        std::vector<std::pair<std::uint64_t, int>> rows;
        for (auto [entity, offspring, ordinal] : registry.view<Offspring, CreationOrdinal>().each()) {
            rows.emplace_back(ordinal.value, offspring.origin);
        }
        std::ranges::sort(rows);
        std::vector<int> origins;
        for (const auto& row : rows) {
            origins.push_back(row.second);
        }
        return origins;
    }
};

}  // namespace

TEST_CASE("an emitting pass queues events in creation order after a destroy", "[runtime][codegen-entt][rule-order-by]") {
    World world;
    CHECK(world.run_frame().emitted == 234);
}

TEST_CASE("limit without order by takes the oldest rows", "[runtime][codegen-entt][rule-order-by]") {
    World world;
    world.run_frame();
    CHECK(world.picked_ids() == std::vector<int>{2, 3});
}

TEST_CASE("a spawning pass creates entities in its rows' creation order", "[runtime][codegen-entt][rule-order-by]") {
    World world;
    world.run_frame();
    CHECK(world.offspring_origins_in_creation_order() == std::vector<int>{2, 3, 4});
}

TEST_CASE("many rows writing one named field leave the newest row's value", "[runtime][codegen-entt][rule-order-by]") {
    World world;
    CHECK(world.run_frame().newest == 4);
}

TEST_CASE("query.first returns the oldest match after a destroy", "[runtime][codegen-entt][stdlib-world-query]") {
    World world;
    CHECK(world.run_frame().first == 2);
}

TEST_CASE("query.all lists matches in creation order", "[runtime][codegen-entt][stdlib-world-query]") {
    World world;
    CHECK(world.run_frame().all == 234);
}

TEST_CASE("a self-only pass still visits every row once", "[runtime][codegen-entt][rule-order-by]") {
    World world;
    world.run_frame();
    for (auto [entity, item] : world.registry.view<Item>().each()) {
        CHECK(item.steps == 1);
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
