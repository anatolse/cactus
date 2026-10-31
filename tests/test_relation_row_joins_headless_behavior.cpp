#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace game    = cactus_gen::relation_row_joins;
namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
    }

    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().relation_row_joins;
    }

    game::Control& control() {
        return registry.get<game::Control>(slots().Settings);
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    std::string run(game::Query query) {
        control().query = query;
        control().trace.clear();
        tick();
        return control().trace;
    }

    auto& walk() {
        return backend::relation_store<game::Walk>(registry);
    }
};

TEST_CASE("Joined location and path rows select next steps", "[headless][row-join]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);
    const auto& slots = cactus_gen::detail::generated_named_slots().relation_row_joins;
    CHECK(registry.get<game::Robot>(slots.First).steps == 1);
    CHECK(registry.get<game::Robot>(slots.First).distance == 3);
    CHECK(registry.get<game::Robot>(slots.Second).distance == 1);
    const auto& suggestions = backend::relation_store<game::Suggested>(registry);
    CHECK(suggestions.contains(slots.First, slots.Middle));
    CHECK(suggestions.contains(slots.Second, slots.Goal));
}

TEST_CASE("Row joins preserve authored order in either binding direction", "[headless][row-join]") {
    World world;
    const auto& slots = World::slots();
    auto& locations   = backend::relation_store<game::Located>(world.registry);
    locations.clear();
    locations.compact();
    locations.add(slots.Second, slots.Middle, game::Located{});
    locations.add(slots.First, slots.Start, game::Located{});
    CHECK(world.run(game::Query::Goal) == "2;1;");
    CHECK(world.run(game::Query::ReverseGoal) == "1;2;");
    CHECK(backend::derived_relation<game::Route>(world.registry).cached_keys() == 1);
    CHECK(backend::derived_relation<game::Route>(world.registry).builds() == 1);
}

TEST_CASE("Stored self joins agree with the ordered product reference", "[headless][row-join]") {
    World world;
    const auto& slots = World::slots();
    auto& rows        = world.walk();
    rows.clear();
    rows.compact();
    rows.add(slots.Goal, slots.Start, game::Walk{.cost = 7, .rank = 3.0F});
    rows.add(slots.Start, slots.Middle, game::Walk{.cost = 2, .rank = std::numeric_limits<float>::quiet_NaN()});
    rows.add(slots.Other, slots.Middle, game::Walk{.cost = 5});
    rows.add(slots.Middle, slots.Other, game::Walk{.cost = 3, .rank = 1.0F});
    rows.add(slots.Middle, slots.Goal, game::Walk{.cost = 4, .rank = 1.0F});
    rows.add(slots.Start, slots.Start, game::Walk{.cost = 9, .rank = 2.0F});
    rows.add(slots.Other, slots.Other, game::Walk{.cost = 0});
    struct Pair {
        int left;
        int right;
        float rank;
    };
    for (const auto query :
         {game::Query::Chain, game::Query::FromFrom, game::Query::FromTo, game::Query::ToTo, game::Query::Sorted}) {
        std::vector<Pair> expected;
        for (const auto& a : rows.rows()) {
            for (const auto& b : rows.rows()) {
                const auto left  = query == game::Query::FromFrom || query == game::Query::FromTo ? a.from : a.to;
                const auto right = query == game::Query::FromTo || query == game::Query::ToTo ? b.to : b.from;
                if (left == right && (query != game::Query::Sorted || (a.row.cost > 0 && b.row.cost > 0))) {
                    expected.push_back(Pair{a.row.cost, b.row.cost, b.row.rank});
                }
            }
        }
        if (query == game::Query::Sorted) {
            std::ranges::stable_sort(expected, [](const Pair& a, const Pair& b) {
                if (std::isnan(a.rank) || std::isnan(b.rank)) {
                    return !std::isnan(a.rank) && std::isnan(b.rank);
                }
                return a.rank < b.rank;
            });
            expected.resize(std::min(expected.size(), std::size_t{2}));
        }
        std::string trace;
        for (const auto& pair : expected) {
            trace += std::format("{}:{};", pair.left, pair.right);
        }
        CAPTURE(query);
        CHECK(world.run(query) == trace);
    }
    rows.clear();
    rows.compact();
    CHECK(world.run(game::Query::Chain).empty());
}

TEST_CASE("Joined rows keep snapshot values until commit", "[headless][row-join]") {
    World world;
    const auto& slots = World::slots();
    CHECK(world.run(game::Query::Snapshot) == "2:1;1:3;");
    CHECK_FALSE(world.registry.valid(slots.Goal));
    REQUIRE(world.walk().find(slots.Start, slots.Middle) != nullptr);
    CHECK(world.walk().find(slots.Start, slots.Middle)->cost == 12);
    REQUIRE(world.walk().find(slots.Start, slots.Other) != nullptr);
    CHECK(world.walk().find(slots.Start, slots.Other)->cost == 11);
    CHECK(world.run(game::Query::Snapshot).empty());
}

TEST_CASE("Derived self joins retain rows across cache keys", "[headless][row-join]") {
    World world;
    world.run(game::Query::ReachChain);
    const auto& slots    = World::slots();
    const auto& observed = backend::relation_store<game::Observed>(world.registry);
    CHECK(observed.contains(slots.Middle, slots.Goal));
    CHECK(observed.contains(slots.Other, slots.Goal));
    CHECK(observed.size() == 2);
    CHECK(world.registry.get<game::Tile>(slots.Middle).hits == 1);
    CHECK(world.registry.get<game::Tile>(slots.Other).hits == 1);
    CHECK(backend::derived_relation<game::Reach>(world.registry).cached_keys() == 4);
}

TEST_CASE("Components and kept rows can drive route joins", "[headless][row-join]") {
    World world;
    world.run(game::Query::Components);
    const auto& slots    = World::slots();
    const auto& observed = backend::relation_store<game::Observed>(world.registry);
    CHECK(observed.contains(slots.Start, slots.Middle));
    CHECK(observed.contains(slots.Middle, slots.Goal));
    CHECK(observed.contains(slots.Other, slots.Goal));
    CHECK(observed.size() == 3);
    CHECK(world.run(game::Query::Kept) == "1;2;");
    backend::relation_store<game::Located>(world.registry).erase_from(slots.First);
    CHECK(world.run(game::Query::Kept) == "1;2;");
    CHECK(world.run(game::Query::Kept) == "2;");
}

TEST_CASE("A restore refreshes joined locations and paths", "[headless][row-join]") {
    World world;
    const auto gap = backend::create_entity(world.registry);
    world.registry.emplace<backend::CreationOrdinal>(gap, backend::generated_next_creation_ordinal());
    world.control().recruit = true;
    world.tick();
    world.tick();
    const auto recruited = [&] {
        for (const auto [entity, robot] : world.registry.view<game::Robot>().each()) {
            if (robot.number == 3) {
                return entity;
            }
        }
        return static_cast<entt::entity>(entt::null);
    };
    const auto old_recruit = recruited();
    REQUIRE((old_recruit != entt::null));
    const auto snapshot    = backend::generated_capture_world_snapshot(world.registry);
    const auto old_first   = World::slots().First;
    const auto saved_steps = world.registry.get<game::Robot>(old_first).steps;
    backend::relation_store<game::Located>(world.registry).erase_from(old_first);
    world.walk().add(World::slots().Start, World::slots().Goal, game::Walk{});
    world.tick();
    const auto restored = backend::generated_restore_world(world.registry, snapshot);
    REQUIRE(restored.ok);
    world.run(game::Query::Goal);
    const auto& slots      = World::slots();
    const auto new_recruit = recruited();
    REQUIRE((new_recruit != entt::null));
    CHECK(new_recruit != old_recruit);
    CHECK(backend::relation_store<game::Located>(world.registry).contains(slots.First, slots.Start));
    CHECK(world.registry.get<game::Robot>(slots.First).distance == 3);
    CHECK(world.registry.get<game::Robot>(slots.First).steps == saved_steps + 1);
    CHECK(backend::relation_store<game::Suggested>(world.registry).contains(slots.First, slots.Middle));
    CHECK(backend::relation_store<game::Suggested>(world.registry).contains(new_recruit, slots.Middle));
}

TEST_CASE("Twenty locations share one goal cache", "[headless][row-join]") {
    World world;
    const auto& slots = World::slots();
    auto& locations   = backend::relation_store<game::Located>(world.registry);
    for (int number = 3; number <= 20; ++number) {
        const auto entity = backend::create_entity(world.registry);
        world.registry.emplace<backend::CreationOrdinal>(entity, backend::generated_next_creation_ordinal());
        world.registry.emplace<game::Robot>(entity, game::Robot{.number = number});
        locations.add(entity, slots.Start, game::Located{});
    }
    world.tick();
    for (const auto [entity, robot] : world.registry.view<game::Robot>().each()) {
        CHECK(robot.steps == 1);
    }
    auto& routes = backend::derived_relation<game::Route>(world.registry);
    CHECK(routes.builds() == 1);
    CHECK(routes.cached_keys() == 1);
}

TEST_CASE("Row joins receive untargeted events and ignore targeted events", "[headless][row-join]") {
    World world;
    CHECK(world.run(game::Query::Events) == "2:1;1:3;");
    world.control().targeted = true;
    CHECK(world.run(game::Query::Events).empty());
}

TEST_CASE("Two endpoint equalities emit each row pair once", "[headless][row-join]") {
    World world;
    CHECK(world.run(game::Query::BothEnds) == "2:2;1:1;1:1;3:3;");
}

TEST_CASE("Unlimited joined predicates read live named fields in logical order", "[headless][row-join]") {
    World world;
    CHECK(world.run(game::Query::LiveWhere) == "2:1;");
    CHECK(world.control().seen == 1);
    const auto& slots = World::slots();
    auto& locations = backend::relation_store<game::Located>(world.registry);
    locations.clear();
    locations.compact();
    locations.add(slots.Second, slots.Middle, game::Located{});
    locations.add(slots.First, slots.Start, game::Located{});
    world.control().seen = 0;
    CHECK(world.run(game::Query::LiveGoal) == "1;");
    CHECK(world.control().seen == 1);
    CHECK(world.registry.get<game::Robot>(slots.First).steps == 1);
    CHECK(world.registry.get<game::Robot>(slots.Second).steps == 0);
    world.control().seen = 0;
    CHECK(world.run(game::Query::LiveLimited) == "2:1;1:3;");
    CHECK(world.control().seen == 2);
}

TEST_CASE("Relation-tested entity pairs keep product order from either end", "[headless][row-join]") {
    World world;
    const auto& slots = World::slots();
    auto& locations   = backend::relation_store<game::Located>(world.registry);
    locations.clear();
    locations.compact();
    locations.add(slots.Second, slots.Goal, game::Located{});
    locations.add(slots.First, slots.Other, game::Located{});
    locations.add(slots.Second, slots.Middle, game::Located{});
    locations.add(slots.First, slots.Start, game::Located{});
    CHECK(world.run(game::Query::TestedPairs) == "1>1;1>3;2>2;2>4;");
    CHECK(world.run(game::Query::ReverseTestedPairs) == "1>1;2>2;1>3;2>4;");
}

TEST_CASE("A union of identity and relation branches keeps product order and runs each pair once",
          "[headless][row-join][union]") {
    World world;
    const auto& slots = World::slots();
    world.walk().add(slots.Goal, slots.Goal, game::Walk{});
    world.walk().add(slots.Other, slots.Start, game::Walk{});
    std::string expected;
    for (const auto tile : {slots.Start, slots.Middle, slots.Other, slots.Goal}) {
        for (const auto next : {slots.Start, slots.Middle, slots.Other, slots.Goal}) {
            if (next == tile || world.walk().contains(tile, next)) {
                expected += std::format("{}>{};", world.registry.get<game::Tile>(tile).label,
                                        world.registry.get<game::Tile>(next).label);
            }
        }
    }
    CHECK(expected == "1>1;1>2;1>3;2>2;2>4;3>1;3>3;3>4;4>4;");
    CHECK(world.run(game::Query::UnionPairs) == expected);
}

}  // namespace
