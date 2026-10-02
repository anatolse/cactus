// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

using Readings = const_tables__Readings;
using Roster   = const_tables__Roster;

template <typename Trait>
entt::entity only_entity(entt::registry& registry) {
    auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return *view.begin();
}

const Roster& roster_tagged(entt::registry& registry, int tag) {
    for (const auto entity : registry.view<Roster>()) {
        if (registry.get<Roster>(entity).tag == tag) {
            return registry.get<Roster>(entity);
        }
    }
    FAIL("no Roster with tag " << tag);
    return registry.get<Roster>(*registry.view<Roster>().begin());
}

}  // namespace

TEST_CASE("const expressions and const tables hold their values at runtime", "[runtime][const-tables]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    cactus_headless_test::drive_frame(registry);

    const auto& readings = registry.get<Readings>(only_entity<Readings>(registry));

    SECTION("scalar constants built from operators, other constants, and pure calls") {
        CHECK(readings.half == Catch::Approx(1.0F));
        CHECK(readings.two_pi == Catch::Approx(6.2831853F));
        CHECK(readings.root_two == Catch::Approx(1.4142135F));
        CHECK(readings.steer == Catch::Approx(3.1415927F));
        CHECK(readings.six == Catch::Approx(6.0F));
        CHECK(readings.origin_y == Catch::Approx(2.0F));
    }

    SECTION("a struct constant's field is readable") {
        CHECK(readings.robot_speed == Catch::Approx(4.0F));
    }

    SECTION("a list constant is iterated in order") {
        CHECK(readings.wave_count == 2);
        CHECK(readings.wave_health == 9);
        CHECK(readings.first_clip == 6);
        CHECK(readings.none_count == 0);
    }

    SECTION("a struct read from a constant is a copy") {
        CHECK(readings.copy_speed == Catch::Approx(1.0F));
        CHECK(readings.robot_after_copy == Catch::Approx(4.0F));
    }

    SECTION("same-named constants of two modules stay distinct") {
        CHECK(readings.local_speed == Catch::Approx(5.0F));
        CHECK(readings.imported_speed == Catch::Approx(2.0F));
        CHECK(readings.rifle_range == Catch::Approx(20.0F));
    }

    SECTION("window configuration reads a computed constant") {
        CHECK(cactus::runtime::entt_backend::generated_project_config().window_width == 1280);
    }

    SECTION("a field of a constructed value is readable") {
        CHECK(readings.built_health == 7);
    }

    SECTION("a trait default may read a constant") {
        CHECK(registry.get<const_tables__Mover>(only_entity<const_tables__Mover>(registry)).speed ==
              Catch::Approx(3.0F));
    }

    SECTION("a trait default may construct a struct") {
        CHECK(roster_tagged(registry, 9).unit.speed == Catch::Approx(0.5F));
        CHECK(roster_tagged(registry, 9).unit.health == 1);
    }

    SECTION("struct construction in add, set, and spawn blocks") {
        CHECK(roster_tagged(registry, 1).unit.health == 10);
        CHECK(roster_tagged(registry, 2).unit.health == 20);
        CHECK(roster_tagged(registry, 2).unit.run_clip == 2);
        CHECK(roster_tagged(registry, 3).unit.health == 30);
        CHECK(roster_tagged(registry, 3).unit.speed == Catch::Approx(3.0F));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
