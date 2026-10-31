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
#include <format>
#include <string>
#include <vector>

namespace {

namespace game    = cactus_gen::grid_region_joins;
namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    game::Control& control() {
        return registry.get<game::Control>(cactus_gen::detail::generated_named_slots().grid_region_joins.Settings);
    }

    std::string run(game::Query query) {
        control().query = query;
        control().trace.clear();
        tick();
        return control().trace;
    }

    template <typename Trait>
    std::vector<entt::entity> oldest_first() {
        const auto view = registry.view<Trait>();
        std::vector<entt::entity> entities(view.begin(), view.end());
        std::ranges::sort(entities, {}, [&](entt::entity entity) { return backend::creation_ordinal(registry, entity); });
        return entities;
    }

    // The full product, checked by the same predicate the rule states.
    std::string product(game::Query query) {
        std::string trace;
        for (const auto probe : oldest_first<game::Probe>()) {
            const auto& p = registry.get<game::Probe>(probe);
            for (const auto spot : oldest_first<game::Spot>()) {
                const auto& s   = registry.get<game::Spot>(spot);
                const double dx = static_cast<double>(p.x) - s.x - 0.5;
                const double dz = static_cast<double>(p.z) - s.z - 0.5;
                const bool hit  = query == game::Query::Box
                                      ? s.x >= p.lo && s.x <= p.hi && s.z >= p.lo && s.z <= p.hi
                                      : (dx * dx) + (dz * dz) <= 2.25;
                if (hit) {
                    trace += std::format("{}:{},{};", p.name, s.x, s.z);
                }
            }
        }
        return trace;
    }
};

TEST_CASE("Grid region passes visit the product's tuples in its order", "[headless][grid-region]") {
    World world;
    REQUIRE(world.registry.view<game::Spot>().size() == 36);
    for (const auto query : {game::Query::Box, game::Query::Disk}) {
        CAPTURE(query);
        const auto expected = world.product(query);
        CHECK_FALSE(expected.empty());
        CHECK(world.run(query) == expected);
    }
}

TEST_CASE("Grid region indexes follow added, removed and restored cells", "[headless][grid-region]") {
    World world;
    const auto snapshot = backend::generated_capture_world_snapshot(world.registry);
    const auto spots    = world.oldest_first<game::Spot>();
    world.registry.destroy(spots[7]);
    world.registry.destroy(spots[8]);
    CHECK(world.run(game::Query::Box) == world.product(game::Query::Box));
    auto& probe = world.registry.get<game::Probe>(world.oldest_first<game::Probe>().front());
    probe.x     = 3.0F;
    probe.lo    = 0;
    CHECK(world.run(game::Query::Disk) == world.product(game::Query::Disk));
    CHECK(world.run(game::Query::Box) == world.product(game::Query::Box));
    const auto restored = backend::generated_restore_world(world.registry, snapshot);
    REQUIRE(restored.ok);
    REQUIRE(world.registry.view<game::Spot>().size() == 36);
    for (const auto query : {game::Query::Box, game::Query::Disk}) {
        CAPTURE(query);
        CHECK(world.run(query) == world.product(query));
    }
}

}  // namespace
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
