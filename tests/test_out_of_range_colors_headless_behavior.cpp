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
#include <limits>

namespace {

namespace game   = cactus_gen::out_of_range_colors_runtime;
namespace render = cactus_gen::stdlib::render;

constexpr CactusColor kOutOfRange{.r = 1.5F, .g = -0.2F, .b = 0.5F, .a = std::numeric_limits<float>::quiet_NaN()};
constexpr Color kExpected{.r = 255, .g = 0, .b = 128, .a = 0};

auto& slots() {
    return cactus_gen::detail::generated_named_slots().out_of_range_colors_runtime;
}

bool keeps_out_of_range(const CactusColor& color) {
    return color.r == 1.5F && color.g == -0.2F && color.b == 0.5F && std::isnan(color.a);
}

template <typename Recorded, typename Channel>
bool drew_with(Channel Recorded::* channel, const Color expected) {
    return cactus_raylib_fake::find_call<Recorded>(cactus_raylib_fake::call_log(), [&](const Recorded& call) {
               return cactus_raylib_fake::colors_equal(call.*channel, expected);
           }) != nullptr;
}

struct World {
    entt::registry registry;

    World() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        cactus_headless_test::init_and_load(registry);
        registry.get<render::shapes::Shape>(slots().Square).color             = kOutOfRange;
        registry.get<render::text::ScreenLabel>(slots().Hud).color            = kOutOfRange;
        registry.get<render::meshes::Renderer>(slots().Cube).color            = kOutOfRange;
        registry.get<render::models::ModelRenderer>(slots().Statue).color     = kOutOfRange;
        registry.get<game::Probe>(slots().Marker).tint                        = kOutOfRange;
        cactus_headless_test::drive_frame(registry);
    }
};

}  // namespace

TEST_CASE("out-of-range colors: 2D renderers receive each channel clamped and rounded once",
          "[example-behavior][unbound-color-channels]") {
    World world;
    CHECK(drew_with(&cactus_raylib_fake::RecordedDrawRectanglePro::color, kExpected));
    CHECK(drew_with(&cactus_raylib_fake::RecordedDrawTextEx::tint, kExpected));
    CHECK(drew_with(&cactus_raylib_fake::RecordedDrawLineEx::color, kExpected));
}

TEST_CASE("out-of-range colors: 3D renderers receive the tinted material clamped and rounded once",
          "[example-behavior][unbound-color-channels]") {
    World world;
    // The placeholder material of a "blue" mesh is #428BFFFF; the tint multiplies it in float.
    const Color mesh_expected = to_raylib_color(color_from_bytes(66, 139, 255, 255) * kOutOfRange);
    CHECK(mesh_expected.r == 99);
    CHECK(drew_with(&cactus_raylib_fake::RecordedDrawMesh::diffuse_color, mesh_expected));
    CHECK(drew_with(&cactus_raylib_fake::RecordedDrawMesh::diffuse_color, kExpected));
    CHECK(drew_with(&cactus_raylib_fake::RecordedDrawLine3D::color, kExpected));
}

TEST_CASE("out-of-range colors: drawing leaves every stored color unchanged",
          "[example-behavior][unbound-color-channels]") {
    World world;
    const auto& registry = world.registry;
    CHECK(keeps_out_of_range(registry.get<render::shapes::Shape>(slots().Square).color));
    CHECK(keeps_out_of_range(registry.get<render::text::ScreenLabel>(slots().Hud).color));
    CHECK(keeps_out_of_range(registry.get<render::meshes::Renderer>(slots().Cube).color));
    CHECK(keeps_out_of_range(registry.get<render::models::ModelRenderer>(slots().Statue).color));
    CHECK(keeps_out_of_range(registry.get<game::Probe>(slots().Marker).tint));
}
TEST_CASE("out-of-range colors: with_alpha scales alpha with no clamp", "[example-behavior][unbound-color-channels]") {
    World world;
    const auto& probe = world.registry.get<game::AlphaProbe>(slots().Marker);
    CHECK(probe.raised.r == 1.0F);
    CHECK(probe.raised.a == 1.5F);
    CHECK(probe.lowered.a == -0.2F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
