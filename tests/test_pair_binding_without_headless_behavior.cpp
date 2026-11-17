// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

namespace {

namespace game    = cactus_gen::pair_binding_without;
namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;
    std::map<int, entt::entity> units;

    World() {
        cactus_headless_test::init_and_load(registry);
        for (const auto [entity, unit] : registry.view<game::Unit>().each()) {
            units.emplace(unit.id, entity);
        }
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    game::Unit& unit(int id) {
        return registry.get<game::Unit>(units.at(id));
    }

    std::string& trace() {
        return registry.get<game::Log>(cactus_gen::detail::generated_named_slots().pair_binding_without.Book).trace;
    }

    // Clears the trace and every per-pass output, then runs one tick.
    void fresh_tick() {
        trace().clear();
        for (auto& [id, entity] : units) {
            auto& u   = unit(id);
            u.allies  = -1;
            u.targets = -1;
            u.picked  = -1;
        }
        tick();
    }

    [[nodiscard]] bool shielded(int id) const {
        return registry.all_of<game::Shielded>(units.at(id));
    }

    [[nodiscard]] bool out_of_m(int id) const {
        return registry.any_of<game::Shielded, game::Dead>(units.at(id));
    }

    [[nodiscard]] bool targeted(int id) const {
        return registry.all_of<game::Targeted>(units.at(id));
    }

    // What Strike traces: every a, then every m that is not shielded or dead, oldest first.
    std::string expected_trace() {
        std::string text;
        const auto order = backend::creation_ordered(registry, registry.view<game::Unit>());
        for (const auto a : order) {
            for (const auto m : order) {
                const auto& from = registry.get<game::Unit>(a);
                const auto& to   = registry.get<game::Unit>(m);
                if (a != m && from.team == to.team && !registry.any_of<game::Shielded, game::Dead>(m)) {
                    text += std::to_string(from.id) + ">" + std::to_string(to.id) + ";";
                }
            }
        }
        return text;
    }

    // Unshielded teammates of `id`.
    int open_mates(int id) {
        int count = 0;
        for (const auto& [other, entity] : units) {
            count += other != id && other % 2 == id % 2 && !shielded(other) ? 1 : 0;
        }
        return count;
    }

    // The lowest-id teammate of `id`, shielded or not.
    static int first_mate(int id) {
        return id % 2 == 0 ? (id == 0 ? 2 : 0) : (id == 1 ? 3 : 1);
    }

    void check_outputs() {
        CHECK(trace() == expected_trace());
        for (const auto& [id, entity] : units) {
            INFO("unit " << id);
            const bool out = shielded(id);
            CHECK(unit(id).allies == (out ? -1 : 2));
            CHECK(unit(id).targets == open_mates(id));
            CHECK(unit(id).picked == (out ? -1 : first_mate(id)));
            CHECK(targeted(id) == !out);
        }
    }
};

}  // namespace

TEST_CASE("pair without: with no exclusion every unit is a member", "[headless][pair-without]") {
    World world;
    world.fresh_tick();
    CHECK(world.trace() == "0>2;0>4;1>3;1>5;2>0;2>4;3>1;3>5;4>0;4>2;5>1;5>3;");
    world.check_outputs();
}

TEST_CASE("pair without: a shielded unit leaves only the excluding bindings", "[headless][pair-without]") {
    World world;
    world.registry.emplace<game::Shielded>(world.units.at(2));
    world.fresh_tick();
    CHECK(world.trace() == "0>4;1>3;1>5;2>0;2>4;3>1;3>5;4>0;5>1;5>3;");
    CHECK(world.unit(2).allies == -1);
    CHECK(world.unit(2).picked == -1);
    CHECK(world.unit(0).targets == 1);
    CHECK(world.unit(2).targets == 2);
    CHECK(world.unit(0).picked == 2);
    CHECK_FALSE(world.targeted(2));
    world.check_outputs();
}

TEST_CASE("pair without: toggling the excluded trait between passes", "[headless][pair-without]") {
    World world;
    for (const int id : {2, 3, 4}) {
        world.registry.emplace<game::Shielded>(world.units.at(id));
        world.fresh_tick();
        world.check_outputs();
    }
    for (const int id : {3, 2}) {
        world.registry.remove<game::Shielded>(world.units.at(id));
        world.fresh_tick();
        world.check_outputs();
    }
    world.registry.emplace<game::Shielded>(world.units.at(3));
    world.registry.remove<game::Shielded>(world.units.at(4));
    world.fresh_tick();
    world.check_outputs();
    world.registry.remove<game::Shielded>(world.units.at(3));
    world.fresh_tick();
    world.check_outputs();
}

TEST_CASE("pair without: membership follows the commit of an add", "[headless][pair-without]") {
    World world;
    world.unit(1).raise = true;
    world.fresh_tick();
    CHECK(world.shielded(1));
    CHECK(world.trace() == "0>2;0>4;1>3;1>5;2>0;2>4;3>1;3>5;4>0;4>2;5>1;5>3;");
    world.fresh_tick();
    world.check_outputs();
    CHECK_FALSE(world.targeted(1));

    world.unit(1).lower = true;
    world.fresh_tick();
    CHECK_FALSE(world.shielded(1));
    world.fresh_tick();
    world.check_outputs();
    CHECK(world.targeted(1));
}

TEST_CASE("pair without: an excluded state variant leaves only that variant out", "[headless][pair-without]") {
    World world;
    for (const int id : {0, 1, 2, 3, 4, 5}) {
        CHECK(world.unit(id).rallied == 2);
    }
    REQUIRE(world.registry.all_of<game::Stance::Ready>(world.units.at(4)));
    REQUIRE_FALSE(world.registry.any_of<game::Stance>(world.units.at(0)));
    world.unit(4).rest = true;
    world.tick();
    REQUIRE(world.registry.all_of<game::Stance::Resting>(world.units.at(4)));
    world.tick();
    CHECK(world.unit(0).rallied == 1);
    CHECK(world.unit(2).rallied == 1);
    CHECK(world.unit(4).rallied == 2);
    for (const int id : {1, 3, 5}) {
        CHECK(world.unit(id).rallied == 2);
    }
}

TEST_CASE("pair without: every listed trait excludes", "[headless][pair-without]") {
    World world;
    world.unit(4).fall = true;
    world.tick();
    REQUIRE(world.out_of_m(4));
    world.fresh_tick();
    CHECK(world.trace() == "0>2;1>3;1>5;2>0;3>1;3>5;4>0;4>2;5>1;5>3;");
    CHECK(world.unit(4).allies == 2);
    CHECK(world.targeted(4));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
