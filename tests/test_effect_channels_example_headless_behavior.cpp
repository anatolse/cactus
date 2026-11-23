// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <initializer_list>

namespace {

namespace fake = cactus_raylib_fake;

using WorldTransform = cactus_gen::stdlib::transform::flat::WorldTransform;
using Shape          = cactus_gen::stdlib::render::shapes::Shape;
using Effect         = cactus_gen::stdlib::render::effects::Effect;
using Shake          = cactus_gen::by_hand::Shake;

constexpr int kFramesPastEveryEffect = 70;

auto& slots() {
    return cactus_gen::detail::generated_named_slots().main;
}

// The rectangle drawn nearest to `near`; boxes stay far apart, so it is that box's.
const fake::RecordedDrawRectanglePro* box_near(const Vector2 near) {
    return fake::find_call<fake::RecordedDrawRectanglePro>(fake::call_log(), [&](const auto& call) {
        return std::abs(call.rec.x - near.x) < 50.0F && std::abs(call.rec.y - near.y) < 50.0F;
    });
}

struct World {
    entt::registry registry;

    World() {
        fake::reset();
        fake::set_window_ready(true);
        cactus_headless_test::init_and_load(registry);
        frame();
    }

    void frame() {
        fake::clear_call_log();
        cactus_headless_test::drive_frame(registry);
    }

    void frames(int count) {
        for (int index = 0; index < count; ++index) {
            frame();
        }
    }

    void press(std::initializer_list<int> keys) {
        for (const int key : keys) {
            fake::set_key_down(key, true);
            fake::set_key_pressed_this_frame(key, true);
        }
        frame();
        for (const int key : keys) {
            fake::set_key_pressed_this_frame(key, false);
            fake::set_key_down(key, false);
        }
    }

    [[nodiscard]] std::size_t effects() const {
        return registry.view<Effect>().size();
    }
};

const Vector2 kHero{.x = -200.0F, .y = -100.0F};
const Vector2 kGhost{.x = 0.0F, .y = -100.0F};
const Vector2 kLamp{.x = 200.0F, .y = -100.0F};
const Vector2 kLeft{.x = -100.0F, .y = 100.0F};
const Vector2 kRight{.x = 100.0F, .y = 100.0F};

}  // namespace

TEST_CASE("effect channels example: a jolt and a flash together shift and brighten one box",
          "[example-behavior][effect-channels]") {
    World world;
    world.press({KEY_J, KEY_F});
    const auto* hero = box_near(kHero);
    REQUIRE(hero != nullptr);
    CHECK(hero->rec.x > kHero.x);
    CHECK(hero->rec.y == kHero.y);
    CHECK(hero->color.r > 64);
    CHECK(fake::approx_equal(world.registry.get<WorldTransform>(slots().Hero).position, kHero));
    CHECK(fake::colors_equal(to_raylib_color(world.registry.get<Shape>(slots().Hero).color),
                             Color{.r = 64, .g = 128, .b = 255, .a = 255}));
}

TEST_CASE("effect channels example: a jolt eases back to the box's place", "[example-behavior][effect-channels]") {
    World world;
    world.press({KEY_J});
    const auto* hero = box_near(kHero);
    REQUIRE(hero != nullptr);
    float last = hero->rec.x - kHero.x;
    CHECK(last > 0.0F);
    for (int step = 0; step < 6; ++step) {
        world.frame();
        const auto* moved = box_near(kHero);
        REQUIRE(moved != nullptr);
        const float offset = moved->rec.x - kHero.x;
        CHECK(offset < last);
        last = offset;
    }
}

TEST_CASE("effect channels example: timed effects end by themselves", "[example-behavior][effect-channels]") {
    World world;
    const auto lasting = world.effects();
    world.press({KEY_J, KEY_F, KEY_P, KEY_O});
    CHECK(world.effects() == lasting + 4);
    world.frames(kFramesPastEveryEffect);
    CHECK(world.effects() == lasting);
    const auto* hero = box_near(kHero);
    REQUIRE(hero != nullptr);
    CHECK(fake::approx_equal(Vector2{.x = hero->rec.x, .y = hero->rec.y}, kHero));
    CHECK(hero->rec.width == 60.0F);
    CHECK(fake::colors_equal(hero->color, Color{.r = 64, .g = 128, .b = 255, .a = 255}));
    const auto* ghost = box_near(kGhost);
    REQUIRE(ghost != nullptr);
    CHECK(ghost->color.a == 255);
}

TEST_CASE("effect channels example: a pop-in starts small", "[example-behavior][effect-channels]") {
    World world;
    world.press({KEY_P});
    const auto* hero = box_near(kHero);
    REQUIRE(hero != nullptr);
    CHECK(hero->rec.width < 60.0F);
}

TEST_CASE("effect channels example: a fade-out dims the box", "[example-behavior][effect-channels]") {
    World world;
    world.press({KEY_O});
    world.frames(30);
    const auto* ghost = box_near(kGhost);
    REQUIRE(ghost != nullptr);
    CHECK(ghost->color.a < 160);
}

TEST_CASE("effect channels example: the highlight is permanent", "[example-behavior][effect-channels]") {
    World world;
    world.frames(kFramesPastEveryEffect);
    const auto* lamp = box_near(kLamp);
    REQUIRE(lamp != nullptr);
    CHECK(lamp->color.r > lamp->color.b);
    CHECK(fake::colors_equal(to_raylib_color(world.registry.get<Shape>(slots().Lamp).color),
                             Color{.r = 128, .g = 128, .b = 128, .a = 255}));
}

TEST_CASE("effect channels example: the hand-made jolt draws the same pose as the stdlib jolt",
          "[example-behavior][effect-channels]") {
    World world;
    world.press({KEY_T});
    REQUIRE(world.registry.view<Shake>().size() == 1);
    bool moved = false;
    for (int step = 0; step < 16; ++step) {
        const auto* left  = box_near(kLeft);
        const auto* right = box_near(kRight);
        REQUIRE(left != nullptr);
        REQUIRE(right != nullptr);
        CHECK(left->rec.x - kLeft.x == right->rec.x - kRight.x);
        CHECK(left->rec.y == right->rec.y);
        moved = moved || left->rec.x != kLeft.x;
        world.frame();
    }
    CHECK(moved);
    CHECK(world.registry.view<Shake>().empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
