// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

namespace game = cactus_gen::relations;
using cactus::runtime::entt_backend::relation_store;

constexpr float kDt = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    [[nodiscard]] static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().relations;
    }
    int ticks() {
        return registry.get<game::Ticks>(slots().Clock).count;
    }
    // Runs `count` fixed ticks, one per 1/60 s frame.
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            const int before = ticks();
            cactus_headless_test::drive_frame(registry, kDt);
            REQUIRE(ticks() == before + 1);
        }
    }
    template <typename Row>
    auto& rows() {
        return relation_store<Row>(registry);
    }
};

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("relations: kept and stored relations", "[runtime][relations]") {
    World world;
    auto& registry    = world.registry;
    const auto& slots = World::slots();

    world.tick(2);

    {  // a kept relation holds one row per touching pair of floor tiles
        auto& walk = world.rows<game::Walk>();
        CHECK(walk.size() == 4);
        CHECK(walk.contains(slots.Heart, slots.Second));
        CHECK(walk.contains(slots.Second, slots.Heart));
        CHECK(walk.contains(slots.Second, slots.Third));
        CHECK(walk.contains(slots.Third, slots.Second));
        CHECK_FALSE(walk.contains(slots.Heart, slots.Third));
        CHECK_FALSE(walk.contains(slots.Third, slots.Rock));
        REQUIRE(walk.find(slots.Second, slots.Third) != nullptr);
        CHECK(walk.find(slots.Second, slots.Third)->cost == 2.5F);
    }

    {  // a trait kept from Walk(Heart, t)
        CHECK(registry.all_of<game::Connected>(slots.Second));
        CHECK_FALSE(registry.all_of<game::Connected>(slots.Third));
        CHECK_FALSE(registry.all_of<game::Connected>(slots.Heart));
    }

    {  // greedy two-sided claims, nearest first, more robots than tasks
        auto& claims = world.rows<game::Claim>();
        CHECK(claims.size() == 2);
        CHECK(claims.contains(slots.Near, slots.Ditch));
        CHECK(claims.contains(slots.Far, slots.Mine));
        CHECK_FALSE(claims.has_from(slots.Middle));
        CHECK(claims.find(slots.Near, slots.Ditch)->done_at == 3);
    }

    {  // a kept relation gains rows as a tile becomes floor
        registry.emplace<game::Floor>(slots.Rock);
        world.tick();
        auto& walk = world.rows<game::Walk>();
        CHECK(walk.size() == 6);
        CHECK(walk.contains(slots.Third, slots.Rock));
        CHECK(walk.contains(slots.Rock, slots.Third));
    }

    {  // `each: Claim as claim` finishes the ditch at tick 3; the claim dies with its task
        CHECK_FALSE(registry.valid(slots.Ditch));
        auto& claims = world.rows<game::Claim>();
        CHECK_FALSE(claims.has_from(slots.Near));
        CHECK(claims.contains(slots.Far, slots.Mine));
    }

    {  // a freed robot claims again
        registry.emplace<game::DigTask>(slots.Spare, game::DigTask{.x = 2.0F, .work = 100});
        world.tick();
        auto& claims = world.rows<game::Claim>();
        CHECK(claims.contains(slots.Near, slots.Spare));
        CHECK_FALSE(claims.has_from(slots.Middle));
    }

    {  // wildcard remove drops every claim of the recalled robot
        registry.get<game::Robot>(slots.Far).recalled = true;
        world.tick();
        CHECK_FALSE(world.rows<game::Claim>().has_from(slots.Far));
        world.tick();
        CHECK(world.rows<game::Claim>().contains(slots.Middle, slots.Mine));
    }

    {  // union: Passable holds walks and bridges
        world.rows<game::Bridge>().add(slots.Third, slots.Island, game::Bridge{});
        world.tick(2);
        auto& passable = world.rows<game::Passable>();
        CHECK(passable.contains(slots.Third, slots.Island));
        CHECK(passable.contains(slots.Heart, slots.Second));
        CHECK(passable.size() == world.rows<game::Walk>().size() + 1);
    }

    {  // a row dies with its end
        registry.destroy(slots.Island);
        CHECK(world.rows<game::Bridge>().size() == 0);
        world.tick(2);
        CHECK(world.rows<game::Passable>().size() == world.rows<game::Walk>().size());
    }

    {  // a paused keeper leaves its rows as they were
        registry.get<game::Control>(slots.Settings).frozen = true;
        registry.remove<game::Floor>(slots.Second);
        world.tick();
        CHECK(world.rows<game::Walk>().contains(slots.Heart, slots.Second));
        registry.get<game::Control>(slots.Settings).frozen = false;
        world.tick(2);
        auto& walk = world.rows<game::Walk>();
        CHECK(walk.size() == 2);
        CHECK_FALSE(walk.contains(slots.Heart, slots.Second));
        CHECK(walk.contains(slots.Third, slots.Rock));
        CHECK_FALSE(registry.all_of<game::Connected>(slots.Second));
    }

    {  // a save records stored rows between saved ends only: no kept rows, no row to a tile or to
       // Spare, whose archetype saves nothing
        world.rows<game::Bridge>().add(slots.Heart, slots.Rock, game::Bridge{});
        REQUIRE(world.rows<game::Claim>().size() == 2);
        const auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
        std::vector<std::string_view> recorded;
        for (const auto& row : snapshot.relation_rows()) {
            recorded.push_back(snapshot.relation_name(row));
        }
        CHECK(recorded == std::vector<std::string_view>{"relations.Claim"});

        const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
        REQUIRE(outcome.ok);
        auto& claims = world.rows<game::Claim>();
        CHECK(claims.size() == 1);
        CHECK(claims.contains(slots.Middle, slots.Mine));
        CHECK_FALSE(claims.has_from(slots.Near));
        CHECK(world.rows<game::Bridge>().size() == 0);

        // Restored rows still die with their ends.
        registry.destroy(slots.Mine);
        CHECK_FALSE(world.rows<game::Claim>().has_from(slots.Middle));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
