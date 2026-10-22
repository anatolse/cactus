// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>


namespace {

using Body      = cactus_gen::stdlib::physics::volume::CharacterBody;
using Transform = cactus_gen::stdlib::transform::volume::WorldTransform;

constexpr float kDt    = 1.0F / 60.0F;
constexpr float kRest  = 1.0F;
constexpr float kRadius = 0.4F;

Catch::Approx near(float value, double margin = 1e-3) {
    return Catch::Approx(value).margin(margin);
}

const auto& slots() {
    return cactus_gen::detail::generated_named_slots();
}

struct World {
    entt::registry registry;

    World() {
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);
    }

    // Runs `count` fixed ticks, one per 1/60 s frame.
    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            const int before = ticks();
            cactus::runtime::entt_backend::generated_inject_external_event(
                cactus_gen::stdlib::core::frameEvent{.dt = kDt});
            cactus::runtime::entt_backend::generated_drain_external_events(registry);
            REQUIRE(ticks() == before + 1);
        }
    }

    int ticks() {
        return registry.get<cactus_gen::character_body::Ticks>(slots().character_body.Clock).count;
    }
    entt::entity child_of(entt::entity parent) {
        for (const auto [entity, link] : registry.view<cactus_gen::stdlib::core::Parent>().each()) {
            if (link.parent == parent) {
                return entity;
            }
        }
        FAIL("no child");
        return entt::null;
    }
    const Vector3& seen(entt::entity entity) {
        return registry.get<cactus_gen::character_body::SeenAt>(entity).position;
    }
    Body& body(entt::entity entity) {
        return registry.get<Body>(entity);
    }
    Vector3& position(entt::entity entity) {
        return registry.get<Transform>(entity).position;
    }
    // Signed gap between two colliders; negative when they overlap.
    float gap(entt::entity a, entt::entity b) {
        const auto a_shape = cactus_collider_shape_of(registry, a);
        const auto b_shape = cactus_collider_shape_of(registry, b);
        REQUIRE(a_shape.has_value());
        REQUIRE(b_shape.has_value());
        return cactus::runtime::physics::separation(*a_shape, *b_shape).distance;
    }
};

}  // namespace

// ── The stdlib moves every 3D CharacterBody ─────────────────────────────────

TEST_CASE("a body moves with no game rule", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto mover = slots().character_body.FreeMover;
    world.tick();
    CHECK(world.position(mover).x == near(3.0F * kDt, 1e-5));
    CHECK(world.position(mover).y == near(kRest, 1e-4));
}

TEST_CASE("a body ignores layers its mask filters out", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    world.tick(30);
    CHECK(world.position(slots().character_body.MaskedMover).x == near(21.5F, 1e-3));
}

// ── Solids and triggers ─────────────────────────────────────────────────────

TEST_CASE("a body walks through a trigger, and a contact rule sees it the same tick",
          "[runtime][codegen-entt][stdlib-character-body][rule-keep]") {
    World world;
    const auto wader = slots().character_body.Wader;
    const auto pool  = slots().character_body.WadePool;
    bool entered     = false;
    for (int i = 0; i < 200; ++i) {
        world.tick();
        const bool overlapping = world.gap(wader, pool) < 0.0F;
        CHECK(world.registry.all_of<cactus_gen::character_body::Wading>(wader) == overlapping);
        entered = entered || overlapping;
        CHECK(world.position(wader).x == near(458.0F + (2.0F * kDt * static_cast<float>(i + 1)), 1e-3));
    }
    CHECK(entered);
    CHECK(world.position(wader).x > 461.0F);
    CHECK_FALSE(world.registry.all_of<cactus_gen::character_body::Wading>(wader));
}

TEST_CASE("standing inside a trigger causes no correction", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto soaker = slots().character_body.Soaker;
    world.tick(3);
    CHECK(world.position(soaker).x == near(470.0F, 1e-6));
    CHECK(world.position(soaker).y == near(10.0F, 1e-6));
}

TEST_CASE("a body without Solid blocks nothing", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto runner = slots().character_body.GhostRunner;
    world.tick(60);
    CHECK(world.position(runner).x == near(479.0F, 1e-3));
}

// ── Game rules around solve ─────────────────────────────────────────────────

TEST_CASE("velocity written this tick moves the body this tick", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto driven = slots().character_body.DrivenBody;
    world.tick();
    CHECK(world.position(driven).x == near(360.0F + (2.0F * kDt), 1e-5));
}

TEST_CASE("a rule after physics.solve sees this tick's grounded", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto watched = slots().character_body.Watched;
    bool landed        = false;
    for (int i = 0; i < 90; ++i) {
        world.tick();
        const bool grounded = world.body(watched).grounded;
        CHECK(world.registry.get<cactus_gen::character_body::GroundWatch>(watched).seen == grounded);
        landed = landed || grounded;
    }
    CHECK(landed);
}

// ── Gravity ─────────────────────────────────────────────────────────────────

TEST_CASE("falling accelerates", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto faller = slots().character_body.Faller;
    world.tick(2);
    CHECK(world.body(faller).velocity.y == near(-20.0F * kDt, 1e-5));
    CHECK(world.position(faller).y < 5.0F);
}

TEST_CASE("a grounded body on a walkable slope does not creep", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto sitter   = slots().character_body.SlopeSitter;
    const Vector3 start = world.position(sitter);
    for (int i = 0; i < 60; ++i) {
        world.tick();
        CHECK(world.position(sitter).x == near(start.x, 1e-5));
        CHECK(world.position(sitter).y == near(start.y, 1e-5));
        CHECK(world.body(sitter).grounded);
    }
}

// ── Collisions ──────────────────────────────────────────────────────────────

TEST_CASE("a fast body stops on the near side of a thin wall", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto body = slots().character_body.ThinWaller;
    world.tick(30);
    CHECK(world.position(body).x + kRadius <= 82.0F - 0.025F + 1e-3F);
    CHECK(world.position(body).x > 81.0F);
}

TEST_CASE("diagonal motion slides along a wall", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto slider = slots().character_body.WallSlider;
    world.tick(30);
    const float x = world.position(slider).x;
    world.tick();
    CHECK(world.position(slider).x == near(x + (2.0F * kDt), 1e-4));
    CHECK(world.position(slider).z == near(-0.9F + kRadius, 1e-3));
    CHECK(world.body(slider).velocity.z == near(0.0F, 1e-3));
    CHECK(world.body(slider).velocity.x == near(2.0F, 1e-5));
    CHECK(world.body(slider).grounded);
}

TEST_CASE("a corner stops the body without jitter", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto runner = slots().character_body.CornerRunner;
    world.tick(60);
    const Vector3 rest = world.position(runner);
    CHECK(rest.x == near(121.4F - kRadius, 1e-3));
    CHECK(rest.z == near(-0.9F + kRadius, 1e-3));
    world.tick(10);
    CHECK(world.position(runner).x == near(rest.x, 1e-6));
    CHECK(world.position(runner).z == near(rest.z, 1e-6));
}

// ── Grounding ───────────────────────────────────────────────────────────────

TEST_CASE("a falling body lands on a floor", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto lander = slots().character_body.Lander;
    world.tick(60);
    const auto& body = world.body(lander);
    CHECK(world.position(lander).y == near(kRest, 1e-3));
    CHECK(body.grounded);
    CHECK(body.ground_normal.y == near(1.0F, 1e-4));
    CHECK(body.velocity.y == 0.0F);
    CHECK(body.time_since_grounded == 0.0F);
}

TEST_CASE("a body on a slope steeper than max_slope slides down", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto slider = slots().character_body.SteepSlider;
    world.tick(5);
    float y = world.position(slider).y;
    for (int i = 0; i < 10; ++i) {
        world.tick();
        CHECK_FALSE(world.body(slider).grounded);
        CHECK(world.position(slider).y < y);
        y = world.position(slider).y;
    }
}

TEST_CASE("a grounded body can't walk up a slope steeper than max_slope", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto walker = slots().character_body.RampWalker;
    for (int i = 0; i < 150; ++i) {
        world.tick();
        INFO("tick " << i << " x " << world.position(walker).x << " y " << world.position(walker).y);
        CHECK(world.position(walker).y < kRest + 0.1F);
        CHECK(world.position(walker).x < 401.5F);
    }
}

TEST_CASE("time_since_grounded counts airborne ticks", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto walker = slots().character_body.LedgeWalker;
    int airborne = 0;
    for (int i = 0; i < 120 && airborne < 3; ++i) {
        world.tick();
        airborne = world.body(walker).grounded ? 0 : airborne + 1;
    }
    REQUIRE(airborne == 3);
    CHECK(world.body(walker).time_since_grounded == near(3.0F * kDt, 1e-5));
}

// ── Jumping ─────────────────────────────────────────────────────────────────

TEST_CASE("a jump leaves the ground", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto hopper = slots().character_body.Hopper;
    world.tick();
    CHECK(world.position(hopper).y > kRest);
    CHECK_FALSE(world.body(hopper).grounded);
    world.tick(5);
    CHECK(world.position(hopper).y > kRest + 0.1F);
}

TEST_CASE("a ceiling stops a jump", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto hopper = slots().character_body.CeilingHopper;
    world.tick(8);
    CHECK(world.body(hopper).velocity.y <= 0.0F);
    CHECK(world.position(hopper).y + 1.0F <= 2.4F + 1e-3F);
    world.tick(60);
    CHECK(world.position(hopper).y == near(kRest, 1e-3));
    CHECK(world.body(hopper).grounded);
}

// ── Steps and descents ──────────────────────────────────────────────────────

TEST_CASE("a grounded body climbs a low step without leaving the ground", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto climber = slots().character_body.StepClimber;
    for (int i = 0; i < 60; ++i) {
        world.tick();
        INFO("tick " << i << " x " << world.position(climber).x << " y " << world.position(climber).y);
        CHECK(world.body(climber).grounded);
        CHECK(world.body(climber).velocity.y <= 0.0F);
    }
    CHECK(world.position(climber).y == near(kRest + 0.5F, 1e-3));
    CHECK(world.position(climber).x > 242.0F);
}

TEST_CASE("a step taller than step_height blocks the body", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto stepper = slots().character_body.TallStepper;
    for (int i = 0; i < 60; ++i) {
        world.tick();
        CHECK(world.body(stepper).velocity.y <= 0.0F);
    }
    CHECK(world.position(stepper).x == near(261.0F - kRadius, 1e-3));
    CHECK(world.position(stepper).y == near(kRest, 1e-3));
    CHECK(world.body(stepper).grounded);
}

TEST_CASE("a body pressed against a tall block jumps onto it instead of hanging on its edge",
          "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto jumper = slots().character_body.EdgeJumper;
    world.tick(120);
    CHECK(world.position(jumper).y == near(1.8F, 1e-3));
    CHECK(world.position(jumper).x > 420.35F);
    CHECK(world.body(jumper).grounded);
}

TEST_CASE("a grounded body walks down stairs without falling", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto walker = slots().character_body.StairWalker;
    for (int i = 0; i < 150; ++i) {
        world.tick();
        INFO("tick " << i << " x " << world.position(walker).x << " y " << world.position(walker).y);
        CHECK(world.body(walker).grounded);
    }
    CHECK(world.position(walker).y == near(kRest, 1e-3));
    CHECK(world.position(walker).x > 285.0F);
}

TEST_CASE("walking off a high ledge falls over several ticks", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto walker = slots().character_body.LedgeWalker;
    float y = world.position(walker).y;
    int falling = 0;
    for (int i = 0; i < 150; ++i) {
        world.tick();
        const float next = world.position(walker).y;
        CHECK(y - next < 0.5F);
        falling += world.body(walker).grounded ? 0 : 1;
        y = next;
    }
    CHECK(falling > 5);
    CHECK(y == near(kRest, 1e-3));
    CHECK(world.body(walker).grounded);
}

// ── Overlap ─────────────────────────────────────────────────────────────────

TEST_CASE("a body spawned inside a wall is pushed out in one tick", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto body = slots().character_body.WallSpawn;
    const auto wall = slots().character_body.SpawnWall;
    REQUIRE(world.gap(body, wall) < -0.1F);
    world.tick();
    CHECK(world.gap(body, wall) >= -1e-3F);
}

TEST_CASE("two overlapping bodies split the correction", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto left  = slots().character_body.SplitLeft;
    const auto right = slots().character_body.SplitRight;
    world.tick();
    CHECK(world.position(left).x == near(319.6F, 1e-4));
    CHECK(world.position(right).x == near(320.4F, 1e-4));
    CHECK(world.gap(left, right) >= -1e-4F);
}

TEST_CASE("bodies walking into each other stop face to face", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto left  = slots().character_body.WalkerLeft;
    const auto right = slots().character_body.WalkerRight;
    for (int i = 0; i < 120; ++i) {
        world.tick();
        CHECK(world.gap(left, right) >= -(2.0F * kDt) - 1e-4F);
    }
    const float left_x  = world.position(left).x;
    const float right_x = world.position(right).x;
    world.tick(5);
    CHECK(world.position(left).x == near(left_x, 1e-4));
    CHECK(world.position(right).x == near(right_x, 1e-4));
    CHECK(world.position(left).x < world.position(right).x);
}
// ── Children follow bodies ──────────────────────────────────────────────────

TEST_CASE("a rule after physics.solve sees a body's subtree at this tick's pose",
          "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto carrier = slots().character_body.Carrier;
    const auto hurtbox = world.child_of(carrier);
    const auto crest   = world.child_of(hurtbox);
    for (int i = 1; i <= 3; ++i) {
        world.tick();
        const Vector3 body = world.position(carrier);
        CHECK(body.x == near(420.0F + (0.5F * static_cast<float>(i)), 1e-4));
        CHECK(world.seen(hurtbox).x == near(body.x, 1e-5));
        CHECK(world.seen(hurtbox).y == near(body.y + 1.0F, 1e-5));
        CHECK(world.seen(crest).x == near(body.x, 1e-5));
        CHECK(world.seen(crest).y == near(body.y + 1.5F, 1e-5));
    }
}

TEST_CASE("a non-body pose root's subtree waits for late_tick", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto cart = slots().character_body.Cart;
    const auto load = world.child_of(cart);
    world.tick(2);
    CHECK(world.position(cart).x == near(442.0F, 1e-5));
    CHECK(world.seen(load).x == near(441.0F, 1e-5));
    CHECK(world.position(load).x == near(442.0F, 1e-5));
    CHECK(world.position(load).y == near(21.0F, 1e-5));
}

// ── Pausing ─────────────────────────────────────────────────────────────────

namespace {

void check_same_body(const Body& now, const Body& before) {
    CHECK(now.velocity.x == before.velocity.x);
    CHECK(now.velocity.y == before.velocity.y);
    CHECK(now.velocity.z == before.velocity.z);
    CHECK(now.grounded == before.grounded);
    CHECK(now.ground_normal.y == before.ground_normal.y);
    CHECK(now.time_since_grounded == before.time_since_grounded);
    CHECK(now.motion.x == before.motion.x);
    CHECK(now.lift == before.lift);
    CHECK(now.drop == before.drop);
    CHECK(now.ground_offset == before.ground_offset);
}

void check_same_position(const Vector3& now, const Vector3& before) {
    CHECK(now.x == before.x);
    CHECK(now.y == before.y);
    CHECK(now.z == before.z);
}

}  // namespace

TEST_CASE("a paused body in midair keeps its pose and solver state", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto faller        = slots().character_body.PausedFaller;
    const Body before        = world.body(faller);
    const Vector3 at         = world.position(faller);
    world.tick(10);
    check_same_body(world.body(faller), before);
    check_same_position(world.position(faller), at);
    CHECK(world.body(faller).velocity.y == near(-3.0F, 1e-6));
}

TEST_CASE("a paused grounded body keeps its grounding and scratch fields", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto stander = slots().character_body.PausedStander;
    const Body before  = world.body(stander);
    const Vector3 at   = world.position(stander);
    world.tick(5);
    check_same_body(world.body(stander), before);
    check_same_position(world.position(stander), at);
    CHECK(world.body(stander).grounded);
}

TEST_CASE("a resumed body continues from its retained velocity without catch-up",
          "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto faller = slots().character_body.PausedFaller;
    world.tick(30);
    world.body(faller).paused = false;
    world.tick();
    const float velocity = -3.0F - (9.81F * kDt);
    CHECK(world.body(faller).velocity.y == near(velocity, 1e-5));
    CHECK(world.position(faller).y == near(8.0F + (velocity * kDt), 1e-5));
}

TEST_CASE("an active body takes the whole correction from a paused body", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto active        = slots().character_body.ActivePusher;
    const auto blocker       = slots().character_body.PausedBlocker;
    const Vector3 blocker_at = world.position(blocker);
    world.tick();
    CHECK(world.position(active).x == near(39.8F, 1e-3));
    check_same_position(world.position(blocker), blocker_at);
    CHECK(world.gap(active, blocker) > -1e-3F);
}

TEST_CASE("two paused bodies stay overlapped until resumed", "[runtime][codegen-entt][stdlib-character-body]") {
    World world;
    const auto left  = slots().character_body.PausedPairLeft;
    const auto right = slots().character_body.PausedPairRight;
    world.tick(3);
    CHECK(world.position(left).x == 60.0F);
    CHECK(world.position(right).x == near(60.6F, 1e-6));
    CHECK(world.gap(left, right) == near(-0.2F, 1e-4));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
