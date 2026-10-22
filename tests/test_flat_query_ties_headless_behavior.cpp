// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {

using Results = cactus_gen::flat_query_ties::Results;
using cactus::runtime::entt_backend::CreationOrdinal;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    void inject(auto occurrence) {
        cactus::runtime::entt_backend::generated_inject_external_event(occurrence);
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
    }

    entt::entity newest() {
        entt::entity found = entt::null;
        std::uint64_t ordinal{};
        for (const auto [entity, created] : registry.view<CreationOrdinal>().each()) {
            if (found == entt::null || created.value > ordinal) {
                found   = entity;
                ordinal = created.value;
            }
        }
        return found;
    }

    entt::entity box(float x, float y, float w, float h) {
        inject(cactus_gen::flat_query_ties::MakeBoxEvent{.x = x, .y = y, .w = w, .h = h});
        return newest();
    }

    entt::entity circle(float x, float y, float radius) {
        inject(cactus_gen::flat_query_ties::MakeCircleEvent{.x = x, .y = y, .radius = radius});
        return newest();
    }

    [[nodiscard]] int place(entt::entity collider) const {
        return registry.get<cactus_gen::flat_query_ties::Rank>(collider).place;
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("flat collider queries break ties by creation order", "[runtime][physics-flat][native-result-ordering]") {
    World world;

    // Two flush tiles under a falling box that straddles both.
    const auto older_tile = world.box(0.0F, 10.0F, 10.0F, 1.0F);
    world.box(10.0F, 10.0F, 10.0F, 1.0F);
    const auto caster = world.box(5.0F, 0.0F, 10.0F, 1.0F);

    // A circle, then a box, overlapping the sinker equally deep from either side.
    const auto older_circle = world.circle(102.5F, 1.0F, 1.0F);
    world.box(98.5F, 0.0F, 2.0F, 2.0F);
    const auto sinker = world.box(100.0F, 0.0F, 2.0F, 2.0F);

    // Box, circle, box, all overlapping the toucher.
    const auto first  = world.box(199.0F, 0.0F, 2.0F, 2.0F);
    const auto second = world.circle(201.0F, 1.0F, 1.0F);
    const auto third  = world.box(201.0F, 0.0F, 2.0F, 2.0F);
    const auto toucher = world.box(200.0F, 0.0F, 2.0F, 2.0F);

    world.inject(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    world.inject(cactus_gen::flat_query_ties::ProbeEvent{.caster = caster, .sinker = sinker, .toucher = toucher});

    const auto& results =
        world.registry.get<Results>(cactus_gen::detail::generated_named_slots().flat_query_ties.Board);
    CHECK(results.cast == older_tile);
    CHECK(results.deepest == older_circle);
    CHECK(world.place(first) == 1);
    CHECK(world.place(second) == 2);
    CHECK(world.place(third) == 3);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
