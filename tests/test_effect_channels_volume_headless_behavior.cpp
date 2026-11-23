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

using WorldTransform = cactus_gen::stdlib::transform::volume::WorldTransform;

auto& slots() {
    return cactus_gen::detail::generated_named_slots().effect_channels_volume;
}

bool near_color(const Color actual, const Color expected) {
    const auto close = [](unsigned char a, unsigned char b) { return std::abs(int{a} - int{b}) <= 1; };
    return close(actual.r, expected.r) && close(actual.g, expected.g) && close(actual.b, expected.b) &&
           close(actual.a, expected.a);
}

const fake::RecordedDrawMesh* mesh_at(const Vector3 position) {
    return fake::find_call<fake::RecordedDrawMesh>(fake::call_log(), [&](const auto& call) {
        return fake::approx_equal(call.transform, position);
    });
}

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

}  // namespace

TEST_CASE("effect channels volume: shift and scale fold into the drawn pose", "[example-behavior][effect-channels]") {
    World world;
    const auto* lifted = mesh_at(Vector3{.x = -2.0F, .y = 1.0F, .z = 0.0F});
    REQUIRE(lifted != nullptr);
    CHECK(std::abs(lifted->transform.m0 - 2.0F) < 0.001F);
    CHECK(std::abs(lifted->transform.m5 - 2.0F) < 0.001F);
    CHECK(std::abs(lifted->transform.m10 - 2.0F) < 0.001F);
    CHECK(fake::approx_equal(world.registry.get<WorldTransform>(slots().Lifted).position,
                             Vector3{.x = -2.0F, .y = 0.0F, .z = 0.0F}));
}

TEST_CASE("effect channels volume: tint and glow fold into the drawn color", "[example-behavior][effect-channels]") {
    World world;
    const auto* lifted = mesh_at(Vector3{.x = -2.0F, .y = 1.0F, .z = 0.0F});
    REQUIRE(lifted != nullptr);
    CHECK(near_color(lifted->diffuse_color, Color{.r = 192, .g = 128, .b = 128, .a = 128}));
}

TEST_CASE("effect channels volume: a mesh with no effect is drawn as authored", "[example-behavior][effect-channels]") {
    World world;
    const auto* plain = mesh_at(Vector3{.x = 2.0F, .y = 0.0F, .z = 0.0F});
    REQUIRE(plain != nullptr);
    CHECK(std::abs(plain->transform.m0 - 1.0F) < 0.001F);
    CHECK(fake::colors_equal(plain->diffuse_color, Color{.r = 128, .g = 128, .b = 128, .a = 255}));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
