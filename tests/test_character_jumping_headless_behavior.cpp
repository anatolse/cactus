// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
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

namespace game      = cactus_gen::character_jumping;
namespace character = cactus_gen::stdlib::character::flat;
namespace phys      = cactus_gen::stdlib::physics::flat;
namespace tf        = cactus_gen::stdlib::transform::flat;
namespace core      = cactus_gen::stdlib::core;
namespace backend   = cactus::runtime::entt_backend;

using Catch::Approx;

constexpr float kStep = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        backend::generated_reset_scheduler_state();
        backend::clear_runtime_faults();
        cactus_headless_test::init_and_load(registry);
    }

    static auto& slots() { return cactus_gen::detail::generated_named_slots(); }

    void frames(int count, float dt = kStep) {
        for (int frame = 0; frame < count; ++frame) {
            cactus_headless_test::drive_frame(registry, dt);
        }
    }

    // Presses Jump for the next frame.
    void request(entt::entity body) { registry.get<game::Pad>(body).press = true; }

    // Presses Jump, then runs one frame.
    void press(entt::entity body) {
        request(body);
        frames(1);
    }

    // The ground the movement rule reports from the next step on.
    void ground(entt::entity body, bool value) { registry.get<game::Scripted>(body).ground = value; }

    // Leaves the ground, then runs `steps` more steps in the air.
    void walk_off(entt::entity body, int steps) {
        ground(body, false);
        frames(1 + steps);
    }

    [[nodiscard]] game::Log& log(entt::entity body) { return registry.get<game::Log>(body); }
    [[nodiscard]] phys::CharacterBody& body(entt::entity entity) { return registry.get<phys::CharacterBody>(entity); }
    [[nodiscard]] bool waiting(entt::entity body) const { return registry.all_of<character::JumpRequest>(body); }
    [[nodiscard]] bool window(entt::entity body) const { return registry.all_of<character::CoyoteWindow>(body); }
    [[nodiscard]] float& pace() { return registry.get<core::Pace>(slots().stdlib.core.Clock).scale; }
};

auto runner() { return World::slots().character_jumping.Runner; }

}  // namespace

TEST_CASE("a grounded body jumps at the next step and the movement rule sees it", "[runtime][character-jumping]") {
    World world;
    world.frames(2);
    world.press(runner());
    CHECK(world.log(runner()).jumps == 1);
    CHECK(world.log(runner()).jumped_at == 3);
    CHECK(world.log(runner()).seen_velocity.x == 5.0F);
    CHECK(world.log(runner()).seen_velocity.y == -420.0F);
    CHECK_FALSE(world.log(runner()).seen_grounded);
    CHECK_FALSE(world.waiting(runner()));
    world.frames(5);
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("a body without JumpSettings is never touched", "[runtime][character-jumping]") {
    World world;
    const auto bystander = World::slots().character_jumping.Bystander;
    world.press(bystander);
    world.frames(3);
    CHECK(world.log(bystander).jumps == 0);
    CHECK(world.body(bystander).velocity.x == 5.0F);
    CHECK(world.body(bystander).velocity.y == 0.0F);
    CHECK_FALSE(world.waiting(bystander));
    CHECK_FALSE(world.window(bystander));
}

TEST_CASE("the movement rule moves a jumping body once in the same step", "[runtime][character-jumping]") {
    World world;
    const auto hero = World::slots().character_jumping.Hero;
    world.frames(30);
    REQUIRE(world.body(hero).grounded);
    const int moves = world.log(hero).moves;
    const float y   = world.registry.get<tf::WorldTransform>(hero).position.y;
    world.press(hero);
    CHECK(world.log(hero).jumps == 1);
    CHECK(world.log(hero).seen_velocity.y == -300.0F);
    CHECK(world.log(hero).moves == moves + 1);
    const float moved = world.registry.get<tf::WorldTransform>(hero).position.y;
    CHECK(moved == Approx(y + (-300.0F + 900.0F * kStep) * kStep));
    CHECK_FALSE(world.body(hero).grounded);
}

TEST_CASE("a request waits through a frame with no fixed step", "[runtime][character-jumping]") {
    World world;
    world.frames(2);
    world.request(runner());
    world.frames(1, kStep / 4.0F);
    CHECK(world.waiting(runner()));
    CHECK(world.log(runner()).jumps == 0);
    world.frames(1);
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("catch-up steps use one request once", "[runtime][character-jumping]") {
    World world;
    world.frames(2);
    world.request(runner());
    world.frames(1, 3.0F * kStep);
    CHECK(world.log(runner()).steps == 5);
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("a buffered request jumps at the first step that sees the ground", "[runtime][character-jumping]") {
    World world;
    const auto patient = World::slots().character_jumping.Patient;
    world.walk_off(patient, 8);
    REQUIRE_FALSE(world.window(patient));
    world.press(patient);
    const int pressed_at = world.log(patient).steps;
    world.frames(1);
    world.ground(patient, true);
    world.frames(2);
    CHECK(world.log(patient).jumps == 1);
    CHECK(world.log(patient).jumped_at == pressed_at + 3);
}

TEST_CASE("grounded is read from the previous step", "[runtime][character-jumping]") {
    World world;
    world.walk_off(runner(), 8);
    world.ground(runner(), true);
    world.press(runner());
    CHECK(world.log(runner()).jumps == 0);
    world.frames(1);
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("a request is last tried 100ms after its first step", "[runtime][character-jumping]") {
    World world;
    world.walk_off(runner(), 8);
    world.press(runner());
    world.frames(4);
    world.ground(runner(), true);
    world.frames(2);
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("a request expires at the exact step", "[runtime][character-jumping]") {
    World world;
    world.walk_off(runner(), 8);
    world.press(runner());
    world.frames(5);
    CHECK(world.waiting(runner()));
    world.ground(runner(), true);
    world.frames(1);
    CHECK_FALSE(world.waiting(runner()));
    world.frames(5);
    CHECK(world.log(runner()).jumps == 0);
}

TEST_CASE("a zero buffer still tries the first step", "[runtime][character-jumping]") {
    World world;
    const auto quick = World::slots().character_jumping.Quick;
    world.frames(2);
    world.press(quick);
    CHECK(world.log(quick).jumps == 1);
    world.walk_off(quick, 8);
    world.press(quick);
    CHECK_FALSE(world.waiting(quick));
}

TEST_CASE("a repeated request starts its age again", "[runtime][character-jumping]") {
    World world;
    world.walk_off(runner(), 8);
    world.press(runner());
    world.frames(4);
    world.press(runner());
    world.frames(3);
    world.ground(runner(), true);
    world.frames(2);
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("a body that walked off an edge may jump inside the window", "[runtime][character-jumping]") {
    World world;
    world.frames(2);
    world.walk_off(runner(), 2);
    world.press(runner());
    CHECK(world.log(runner()).jumps == 1);
    CHECK_FALSE(world.window(runner()));
}

TEST_CASE("the window's last step is 100ms after leaving the ground", "[runtime][character-jumping]") {
    World world;
    world.frames(2);
    world.walk_off(runner(), 6);
    CHECK(world.window(runner()));
    world.press(runner());
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("a body may not jump once the window has passed", "[runtime][character-jumping]") {
    World world;
    world.frames(2);
    world.walk_off(runner(), 7);
    CHECK_FALSE(world.window(runner()));
    world.press(runner());
    CHECK(world.log(runner()).jumps == 0);
    world.ground(runner(), true);
    world.frames(1);
    CHECK(world.log(runner()).jumps == 0);
    world.frames(1);
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("a body jumps once in the air and again after landing", "[runtime][character-jumping]") {
    World world;
    world.frames(2);
    world.ground(runner(), false);
    world.press(runner());
    CHECK(world.log(runner()).jumps == 1);
    world.frames(2);
    world.press(runner());
    world.frames(1);
    CHECK(world.log(runner()).jumps == 1);
    world.frames(10);
    world.ground(runner(), true);
    world.frames(2);
    CHECK(world.window(runner()));
    world.walk_off(runner(), 2);
    world.press(runner());
    CHECK(world.log(runner()).jumps == 2);
}

TEST_CASE("a body that has never been grounded has no window", "[runtime][character-jumping]") {
    World world;
    const auto floater = World::slots().character_jumping.Floater;
    CHECK_FALSE(world.window(floater));
    world.press(floater);
    world.frames(3);
    CHECK(world.log(floater).jumps == 0);
    CHECK_FALSE(world.window(floater));
}

TEST_CASE("negative windows act as zero", "[runtime][character-jumping]") {
    const auto eager = World::slots().character_jumping.Eager;
    {
        World world;
        world.frames(2);
        world.walk_off(eager, 0);
        world.press(eager);
        CHECK(world.log(eager).jumps == 1);
    }
    World world;
    world.frames(2);
    world.walk_off(eager, 1);
    world.press(eager);
    CHECK(world.log(eager).jumps == 0);
    CHECK_FALSE(world.waiting(eager));
}

TEST_CASE("stopped game time keeps a request and a window", "[runtime][character-jumping]") {
    {
        World world;
        world.frames(2);
        world.pace() = 0.0F;
        world.press(runner());
        world.frames(5);
        CHECK(world.waiting(runner()));
        CHECK(world.log(runner()).jumps == 0);
        world.pace() = 1.0F;
        world.frames(1);
        CHECK(world.log(runner()).jumps == 1);
    }
    World world;
    world.frames(2);
    world.walk_off(runner(), 3);
    world.pace() = 0.0F;
    world.frames(20);
    world.pace() = 1.0F;
    world.frames(3);
    CHECK(world.window(runner()));
    world.press(runner());
    CHECK(world.log(runner()).jumps == 1);
}

TEST_CASE("slow motion tries a request on the same steps over more frames", "[runtime][character-jumping]") {
    World world;
    world.walk_off(runner(), 8);
    world.pace() = 0.5F;
    world.request(runner());
    const int before = world.log(runner()).steps;
    int frames       = 0;
    while (world.waiting(runner()) || frames == 0) {
        world.frames(1);
        ++frames;
        REQUIRE(frames < 40);
    }
    CHECK(world.log(runner()).steps - before == 7);
    CHECK(frames >= 13);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
