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

namespace {

namespace fake = cactus_raylib_fake;

using WorldTransform = cactus_gen::stdlib::transform::flat::WorldTransform;
using Shape        = cactus_gen::stdlib::render::shapes::Shape;
using SeenRing       = cactus_gen::effect_channels::SeenRing;

constexpr float kRadToDeg = 57.29577951F;

auto& slots() {
    return cactus_gen::detail::generated_named_slots().effect_channels;
}

bool near_color(const Color actual, const Color expected) {
    const auto close = [](unsigned char a, unsigned char b) { return std::abs(int{a} - int{b}) <= 1; };
    return close(actual.r, expected.r) && close(actual.g, expected.g) && close(actual.b, expected.b) &&
           close(actual.a, expected.a);
}

const fake::RecordedDrawRectanglePro* rectangle_at(const Vector2 position) {
    return fake::find_call<fake::RecordedDrawRectanglePro>(fake::call_log(), [&](const auto& call) {
        return fake::approx_equal(Vector2{.x = call.rec.x, .y = call.rec.y}, position);
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
};

}  // namespace

TEST_CASE("effect channels: shifts of one target sum", "[example-behavior][effect-channels]") {
    World world;
    CHECK(rectangle_at(Vector2{.x = 103.0F, .y = 102.0F}) != nullptr);
}

TEST_CASE("effect channels: scales of one target multiply per axis", "[example-behavior][effect-channels]") {
    World world;
    const auto* scaled = rectangle_at(Vector2{.x = 200.0F, .y = 100.0F});
    REQUIRE(scaled != nullptr);
    CHECK(std::abs(scaled->rec.width - 10.8F) < 0.001F);
    CHECK(std::abs(scaled->rec.height - 10.8F) < 0.001F);
}

TEST_CASE("effect channels: turns of one target sum", "[example-behavior][effect-channels]") {
    World world;
    const auto* turned = rectangle_at(Vector2{.x = 300.0F, .y = 100.0F});
    REQUIRE(turned != nullptr);
    CHECK(std::abs(turned->rotation - (0.75F * kRadToDeg)) < 0.001F);
}

TEST_CASE("effect channels: tints of one target multiply", "[example-behavior][effect-channels]") {
    World world;
    const auto* tinted = rectangle_at(Vector2{.x = 100.0F, .y = 200.0F});
    REQUIRE(tinted != nullptr);
    CHECK(near_color(tinted->color, Color{.r = 128, .g = 128, .b = 64, .a = 128}));
}

TEST_CASE("effect channels: glows of one target add past white", "[example-behavior][effect-channels]") {
    World world;
    const auto* glowing = rectangle_at(Vector2{.x = 200.0F, .y = 200.0F});
    REQUIRE(glowing != nullptr);
    CHECK(fake::colors_equal(glowing->color, Color{.r = 255, .g = 255, .b = 255, .a = 255}));
}

TEST_CASE("effect channels: a channel eases from start to end", "[example-behavior][effect-channels]") {
    World world;
    CHECK(rectangle_at(Vector2{.x = 301.0F, .y = 200.0F}) != nullptr);
}

TEST_CASE("effect channels: a target with no effect is drawn as authored", "[example-behavior][effect-channels]") {
    World world;
    const auto* plain = rectangle_at(Vector2{.x = 400.0F, .y = 200.0F});
    REQUIRE(plain != nullptr);
    CHECK(plain->rec.width == 10.0F);
    CHECK(fake::colors_equal(plain->color, Color{.r = 128, .g = 128, .b = 128, .a = 255}));
}

TEST_CASE("effect channels: the simulation keeps its truth", "[example-behavior][effect-channels]") {
    World world;
    world.frame();
    const auto& pose = world.registry.get<WorldTransform>(slots().Shifted);
    CHECK(fake::approx_equal(pose.position, Vector2{.x = 100.0F, .y = 100.0F}));
    CHECK(fake::colors_equal(to_raylib_color(world.registry.get<Shape>(slots().Tinted).color),
                             Color{.r = 128, .g = 128, .b = 128, .a = 255}));
    CHECK(world.registry.get<WorldTransform>(slots().Scaled).scale.x == 1.0F);
}

TEST_CASE("effect channels: a game channel folds like the stdlib ones", "[example-behavior][effect-channels]") {
    World world;
    CHECK(world.registry.get<SeenRing>(slots().Marked).width == 5.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
