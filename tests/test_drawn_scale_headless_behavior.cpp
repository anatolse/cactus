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
#include <string>

namespace {

namespace fake = cactus_raylib_fake;

constexpr float kRadToDeg = 57.29577951F;

struct World {
    entt::registry registry;

    World() {
        fake::reset();
        fake::set_window_ready(true);
        cactus_headless_test::init_and_load(registry);
        fake::clear_call_log();
        cactus_headless_test::drive_frame(registry);
    }
};

const fake::RecordedDrawRectanglePro* rectangle_at(const Vector2 position) {
    return fake::find_call<fake::RecordedDrawRectanglePro>(fake::call_log(), [&](const auto& call) {
        return fake::approx_equal(Vector2{.x = call.rec.x, .y = call.rec.y}, position);
    });
}

const fake::RecordedDrawTextPro* text_called(const std::string& text) {
    return fake::find_call<fake::RecordedDrawTextPro>(fake::call_log(),
                                                      [&](const auto& call) { return call.text == text; });
}

}  // namespace

TEST_CASE("drawn scale: a rectangle grows by its scale and keeps its pivot", "[example-behavior][drawn-scale]") {
    World world;
    const auto* wide = rectangle_at(Vector2{.x = 100.0F, .y = 100.0F});
    REQUIRE(wide != nullptr);
    CHECK(wide->rec.width == 80.0F);
    CHECK(wide->rec.height == 60.0F);
    CHECK(fake::approx_equal(wide->origin, Vector2{.x = 40.0F, .y = 30.0F}));
    CHECK(std::abs(wide->rotation - (0.5F * kRadToDeg)) < 0.001F);
}

TEST_CASE("drawn scale: scale one draws as before", "[example-behavior][drawn-scale]") {
    World world;
    const auto* plain = rectangle_at(Vector2{.x = 300.0F, .y = 100.0F});
    REQUIRE(plain != nullptr);
    CHECK(plain->rec.width == 32.0F);
    CHECK(plain->rec.height == 32.0F);
    CHECK(fake::approx_equal(plain->origin, Vector2{.x = 0.0F, .y = 0.0F}));
    const auto* note = text_called("plain");
    REQUIRE(note != nullptr);
    CHECK(fake::approx_equal(note->position, Vector2{.x = 700.0F, .y = 300.0F}));
}

TEST_CASE("drawn scale: an equal scale keeps a circle round", "[example-behavior][drawn-scale]") {
    World world;
    CHECK(fake::find_call<fake::RecordedDrawCircleV>(fake::call_log(), [](const auto& call) {
              return fake::approx_equal(call.center, Vector2{.x = 97.0F, .y = 300.0F}) && call.radius == 24.0F;
          }) != nullptr);
}

TEST_CASE("drawn scale: an unequal scale draws an ellipse", "[example-behavior][drawn-scale]") {
    World world;
    CHECK(fake::find_call<fake::RecordedDrawEllipseV>(fake::call_log(), [](const auto& call) {
              return fake::approx_equal(call.center, Vector2{.x = 300.0F, .y = 300.0F}) && call.radiusH == 32.0F &&
                     call.radiusV == 16.0F;
          }) != nullptr);
    CHECK(fake::find_call<fake::RecordedDrawCircleV>(fake::call_log(), [](const auto& call) {
              return fake::approx_equal(call.center, Vector2{.x = 300.0F, .y = 300.0F});
          }) == nullptr);
}

TEST_CASE("drawn scale: a sprite grows by its scale", "[example-behavior][drawn-scale]") {
    World world;
    CHECK(fake::find_call<fake::RecordedDrawTexturePro>(fake::call_log(), [](const auto& call) {
              return fake::approx_equal(Vector2{.x = call.dest.x, .y = call.dest.y}, Vector2{.x = 500.0F, .y = 100.0F}) &&
                     call.dest.width == 32.0F && call.dest.height == 64.0F;
          }) != nullptr);
}

TEST_CASE("drawn scale: 2D text stretches per axis around its center", "[example-behavior][drawn-scale]") {
    World world;
    const auto& log = fake::call_log();
    CHECK(fake::find_call<fake::RecordedTranslate>(log, [](const auto& call) {
              return call.x == 500.0F && call.y == 300.0F && call.z == 0.0F;
          }) != nullptr);
    CHECK(fake::find_call<fake::RecordedRotate>(log, [](const auto& call) {
              return std::abs(call.angle - (0.25F * kRadToDeg)) < 0.001F && call.z == 1.0F;
          }) != nullptr);
    CHECK(fake::find_call<fake::RecordedScale>(log, [](const auto& call) {
              return call.x == 2.0F && call.y == 1.0F && call.z == 1.0F;
          }) != nullptr);
    const auto* word = text_called("scaled");
    REQUIRE(word != nullptr);
    CHECK(fake::approx_equal(word->position, Vector2{.x = 0.0F, .y = 0.0F}));
    CHECK(word->rotation == 0.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
