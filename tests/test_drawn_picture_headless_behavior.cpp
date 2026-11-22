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

using Picture        = cactus_gen::drawn_picture::Picture;
using WorldTransform = cactus_gen::stdlib::transform::flat::WorldTransform;
using DrawnPose      = cactus_gen::stdlib::transform::flat::DrawnPose;
using Shape          = cactus_gen::stdlib::render::shapes::Shape;

constexpr float kRadToDeg = 57.29577951F;

auto& slots() {
    return cactus_gen::detail::generated_named_slots().drawn_picture;
}

bool near_color(const Color actual, const Color expected) {
    const auto close = [](unsigned char a, unsigned char b) { return std::abs(int{a} - int{b}) <= 1; };
    return close(actual.r, expected.r) && close(actual.g, expected.g) && close(actual.b, expected.b) &&
           close(actual.a, expected.a);
}

const fake::RecordedDrawRectanglePro* rectangle_near(const Vector2 position) {
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

    void picture_off() {
        for (const auto [entity, picture] : registry.view<Picture>().each()) {
            picture.shown = false;
        }
    }
};

}  // namespace

TEST_CASE("drawn picture: a shape is drawn at its drawn pose and color", "[example-behavior][drawn-picture]") {
    World world;
    const auto* box = rectangle_near(Vector2{.x = 104.0F, .y = 100.0F});
    REQUIRE(box != nullptr);
    CHECK(std::abs(box->rotation - (0.5F * kRadToDeg)) < 0.001F);
    CHECK(near_color(box->color, Color{.r = 192, .g = 128, .b = 128, .a = 128}));
}

TEST_CASE("drawn picture: the simulation keeps its truth", "[example-behavior][drawn-picture]") {
    World world;
    const auto& pose = world.registry.get<WorldTransform>(slots().Box);
    CHECK(fake::approx_equal(pose.position, Vector2{.x = 100.0F, .y = 100.0F}));
    CHECK(pose.rotation == 0.0F);
    CHECK(fake::colors_equal(to_raylib_color(world.registry.get<Shape>(slots().Box).color),
                             Color{.r = 128, .g = 128, .b = 128, .a = 255}));
}

TEST_CASE("drawn picture: with no projection the truth is drawn again", "[example-behavior][drawn-picture]") {
    World world;
    world.picture_off();
    world.frame();
    CHECK_FALSE(world.registry.all_of<DrawnPose>(slots().Box));
    CHECK(rectangle_near(Vector2{.x = 104.0F, .y = 100.0F}) == nullptr);
    const auto* box = rectangle_near(Vector2{.x = 100.0F, .y = 100.0F});
    REQUIRE(box != nullptr);
    CHECK(box->rotation == 0.0F);
    CHECK(fake::colors_equal(box->color, Color{.r = 128, .g = 128, .b = 128, .a = 255}));
}

TEST_CASE("drawn picture: an entity with no picture is drawn as authored", "[example-behavior][drawn-picture]") {
    World world;
    CHECK(fake::find_call<fake::RecordedDrawCircleV>(fake::call_log(), [](const auto& call) {
              return fake::approx_equal(call.center, Vector2{.x = 300.0F, .y = 100.0F}) &&
                     fake::colors_equal(call.color, Color{.r = 0, .g = 255, .b = 0, .a = 255});
          }) != nullptr);
}

TEST_CASE("drawn picture: a sprite is drawn at its drawn pose and color", "[example-behavior][drawn-picture]") {
    World world;
    CHECK(fake::find_call<fake::RecordedDrawTexturePro>(fake::call_log(), [](const auto& call) {
              return fake::approx_equal(Vector2{.x = call.dest.x, .y = call.dest.y}, Vector2{.x = 210.0F, .y = 5.0F}) &&
                     near_color(call.tint, Color{.r = 255, .g = 0, .b = 0, .a = 128});
          }) != nullptr);
}

TEST_CASE("drawn picture: two drawn things on one entity keep their own colors", "[example-behavior][drawn-picture]") {
    World world;
    const auto* sign = rectangle_near(Vector2{.x = 20.0F, .y = -192.0F});
    REQUIRE(sign != nullptr);
    CHECK(near_color(sign->color, Color{.r = 255, .g = 0, .b = 0, .a = 128}));
    CHECK(fake::find_call<fake::RecordedDrawTextPro>(fake::call_log(), [](const auto& call) {
              return fake::approx_equal(call.position, Vector2{.x = 20.0F, .y = -192.0F}) &&
                     near_color(call.tint, Color{.r = 255, .g = 255, .b = 255, .a = 128});
          }) != nullptr);
}

TEST_CASE("drawn picture: a color above white is converted once at output", "[example-behavior][drawn-picture]") {
    World world;
    const auto* hot = rectangle_near(Vector2{.x = -200.0F, .y = 100.0F});
    REQUIRE(hot != nullptr);
    CHECK(fake::colors_equal(hot->color, Color{.r = 255, .g = 255, .b = 255, .a = 255}));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
