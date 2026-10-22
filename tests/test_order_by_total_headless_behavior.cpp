// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

using Log = cactus_gen::order_by_total_runtime::Log;

struct World {
    entt::registry registry;

    World() {
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);
    }

    void add(int id, float v) {
        cactus::runtime::entt_backend::generated_inject_external_event(
            cactus_gen::order_by_total_runtime::ScoredEvent{.id = id, .v = v});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
    }

    entt::entity find(int id) {
        for (const auto [entity, score] : registry.view<cactus_gen::order_by_total_runtime::Score>().each()) {
            if (score.id == id) {
                return entity;
            }
        }
        FAIL("no score with this id");
        return entt::null;
    }

    const Log& run_frame() {
        cactus::runtime::entt_backend::generated_inject_external_event(
            cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
        const auto view = registry.view<Log>();
        REQUIRE(view.begin() != view.end());
        return registry.get<Log>(*view.begin());
    }
};

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

}  // namespace

TEST_CASE("a NaN sort key sorts last ascending and descending", "[runtime][codegen-entt][rule-order-by]") {
    World world;
    world.add(1, 2.0F);
    world.add(2, kNaN);
    world.add(3, 1.0F);
    const auto& log = world.run_frame();
    CHECK(log.ascending == 312);
    CHECK(log.descending == 132);
    CHECK(log.first == 3);
}

TEST_CASE("NaN sort keys tie with each other", "[runtime][codegen-entt][rule-order-by]") {
    World world;
    world.add(1, kNaN);
    world.add(2, 1.0F);
    world.add(3, -kNaN);
    const auto& log = world.run_frame();
    CHECK(log.ascending == 213);
    CHECK(log.descending == 213);
}

TEST_CASE("signed zero sort keys tie in creation order", "[runtime][codegen-entt][rule-order-by]") {
    World world;
    world.add(1, 0.0F);
    world.add(2, -0.0F);
    const auto& log = world.run_frame();
    CHECK(log.ascending == 12);
    CHECK(log.descending == 12);
}

TEST_CASE("equal sort keys follow creation order, not pool order", "[runtime][codegen-entt][rule-order-by]") {
    // Destroying the first entity moves the last one into its pool slot, so pool order is 3, 2.
    World world;
    world.add(1, 5.0F);
    world.add(2, 5.0F);
    world.add(3, 5.0F);
    world.registry.destroy(world.find(1));
    const auto& log = world.run_frame();
    CHECK(log.ascending == 23);
    CHECK(log.descending == 23);
    CHECK(log.first == 2);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
