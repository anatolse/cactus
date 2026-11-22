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

#include <algorithm>

namespace {

namespace game   = cactus_gen::unbound_color_channels;
namespace shapes = cactus_gen::stdlib::render::shapes;

auto& slots() {
    return cactus_gen::detail::generated_named_slots().unbound_color_channels;
}

const CactusColor& shape_color(const entt::registry& registry, entt::entity entity) {
    return registry.get<shapes::Shape>(entity).color;
}

}  // namespace

TEST_CASE("unbound color channels: a blend toward a darker color reaches the middle",
          "[example-behavior][unbound-color-channels]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto& blended = shape_color(registry, slots().BlendSwatch);
    CHECK(blended.r == 0.5F);
    CHECK(blended.g == 0.5F);
    CHECK(blended.b == 0.5F);
    CHECK(blended.a == 1.0F);
}

TEST_CASE("unbound color channels: a multiplicative fade reaches zero alpha",
          "[example-behavior][unbound-color-channels]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_frames(registry, 1.0F / 60.0F, 1000);

    const auto& faded = shape_color(registry, slots().FadeSwatch);
    CHECK(faded.a < 0.0001F);
    CHECK(faded.a > 0.0F);
    CHECK(faded.r == 1.0F);
}

TEST_CASE("unbound color channels: a color brighter than white keeps 2.0 after it is drawn",
          "[example-behavior][unbound-color-channels]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto& bright = shape_color(registry, slots().BrightSwatch);
    CHECK(bright.r == 2.0F);
    CHECK(bright.g == 2.0F);
    CHECK(bright.b == 2.0F);
    CHECK(bright.a == 2.0F);

    const auto& log         = cactus_raylib_fake::call_log();
    const auto bright_draws = std::ranges::count_if(log, [](const auto& entry) {
        const auto* draw = std::get_if<cactus_raylib_fake::RecordedDrawRectanglePro>(&entry);
        return draw != nullptr && draw->rec.x == 250.0F &&
               cactus_raylib_fake::colors_equal(draw->color, Color{.r = 255, .g = 255, .b = 255, .a = 255});
    });
    CHECK(bright_draws == 1);
}

TEST_CASE("unbound color channels: subtracting then adding the same color gives the first color back",
          "[example-behavior][unbound-color-channels]") {
    cactus_raylib_fake::reset();
    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto& sum = registry.get<game::RoundTrip>(slots().Sum);
    CHECK(sum.result.r == sum.a.r);
    CHECK(sum.result.g == sum.a.g);
    CHECK(sum.result.b == sum.a.b);
    CHECK(sum.result.a == sum.a.a);
    CHECK(sum.result.g == 128.0F / 255.0F);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
