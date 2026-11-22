// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

const cactus_gen::color_helpers::Samples& sample() {
    static entt::registry registry;
    static const bool ready = [] {
        cactus_headless_test::init_and_load(registry);
        cactus_headless_test::drive_frame(registry);
        return true;
    }();
    (void)ready;
    return registry.get<cactus_gen::color_helpers::Samples>(cactus_gen::detail::generated_named_slots().color_helpers.Probe);
}

bool same(CactusColor a, CactusColor b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

}  // namespace

TEST_CASE("std.color.mix gives its ends at 0 and 1", "[runtime][stdlib-color]") {
    CHECK(same(sample().start, CactusColor{.r = 0.0F, .g = 0.25F, .b = 0.0F, .a = 1.0F}));
    CHECK(same(sample().finish, CactusColor{.r = 1.0F, .g = 0.0F, .b = 0.5F, .a = 1.0F}));
}

TEST_CASE("std.color.mix blends channels that go up and down", "[runtime][stdlib-color]") {
    CHECK(same(sample().halfway, CactusColor{.r = 0.5F, .g = 0.125F, .b = 0.25F, .a = 1.0F}));
}

TEST_CASE("std.color.mix extrapolates outside 0..1", "[runtime][stdlib-color]") {
    CHECK(same(sample().beyond, CactusColor{.r = 1.5F, .g = -0.125F, .b = 0.75F, .a = 1.0F}));
}

TEST_CASE("std.color.with_alpha scales alpha with no clamp", "[runtime][stdlib-color]") {
    CHECK(same(sample().half_alpha, CactusColor{.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 0.5F}));
    CHECK(sample().raised_alpha.a == 1.5F);
}

TEST_CASE("std.color declares white and clear", "[runtime][stdlib-color]") {
    CHECK(same(sample().white, CactusColor{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F}));
    CHECK(same(sample().clear, CactusColor{}));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
