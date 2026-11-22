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

#include <array>
#include <limits>

namespace {

namespace ease = cactus_gen::stdlib::math::ease;
namespace game = cactus_gen::easing_curves;
namespace backend = cactus::runtime::entt_backend;

using Catch::Approx;
using Curve = ease::Curve;

struct Named {
    Curve curve;
    float (*function)(float);
};

const std::array<Named, 10> kCurves = {{
    {.curve = Curve::Linear, .function = ease::linear},
    {.curve = Curve::InQuad, .function = ease::in_quad},
    {.curve = Curve::OutQuad, .function = ease::out_quad},
    {.curve = Curve::InOutQuad, .function = ease::in_out_quad},
    {.curve = Curve::InCubic, .function = ease::in_cubic},
    {.curve = Curve::OutCubic, .function = ease::out_cubic},
    {.curve = Curve::InOutCubic, .function = ease::in_out_cubic},
    {.curve = Curve::InBack, .function = ease::in_back},
    {.curve = Curve::OutBack, .function = ease::out_back},
    {.curve = Curve::OutBounce, .function = ease::out_bounce},
}};

constexpr int kSamples = 200;

float at(int i) { return static_cast<float>(i) / static_cast<float>(kSamples); }

struct World {
    entt::registry registry;

    explicit World(int frame_count) {
        backend::generated_reset_scheduler_state();
        backend::clear_runtime_faults();
        cactus_headless_test::drive_frames(registry, 1.0F / 60.0F, frame_count);
    }

    static auto& slots() { return cactus_gen::detail::generated_named_slots().easing_curves; }

    [[nodiscard]] game::Fade& fade(entt::entity entity) { return registry.get<game::Fade>(entity); }
};

}  // namespace

TEST_CASE("every easing curve gives exactly 0 at 0 and exactly 1 at 1", "[runtime][stdlib-easing]") {
    for (const Named& c : kCurves) {
        CHECK(c.function(0.0F) == 0.0F);
        CHECK(c.function(1.0F) == 1.0F);
    }
}

TEST_CASE("easing input below 0, NaN and above 1 counts as the nearest end", "[runtime][stdlib-easing]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const Named& c : kCurves) {
        CHECK(c.function(-0.3F) == 0.0F);
        CHECK(c.function(nan) == 0.0F);
        CHECK(c.function(1.7F) == 1.0F);
    }
}

TEST_CASE("easing curves at the middle follow their formulas", "[runtime][stdlib-easing]") {
    CHECK(ease::linear(0.5F) == 0.5F);
    CHECK(ease::in_quad(0.5F) == 0.25F);
    CHECK(ease::out_quad(0.5F) == 0.75F);
    CHECK(ease::in_out_quad(0.5F) == 0.5F);
    CHECK(ease::in_cubic(0.5F) == 0.125F);
    CHECK(ease::out_cubic(0.5F) == 0.875F);
    CHECK(ease::in_out_cubic(0.5F) == 0.5F);
    CHECK(ease::in_back(0.5F) == Approx(-0.0876975F));
    CHECK(ease::out_back(0.5F) == Approx(1.0876975F));
    CHECK(ease::out_bounce(0.5F) == Approx(0.765625F));
}

TEST_CASE("back curves overshoot and bounce stays inside 0..1", "[runtime][stdlib-easing]") {
    bool out_back_above = false;
    bool in_back_below = false;
    for (int i = 1; i < kSamples; ++i) {
        out_back_above = out_back_above || ease::out_back(at(i)) > 1.0F;
        in_back_below = in_back_below || ease::in_back(at(i)) < 0.0F;
        const float bounce = ease::out_bounce(at(i));
        CHECK(bounce >= 0.0F);
        CHECK(bounce <= 1.0F);
    }
    CHECK(out_back_above);
    CHECK(in_back_below);
}

TEST_CASE("apply equals each curve's own function", "[runtime][stdlib-easing]") {
    for (const Named& c : kCurves) {
        for (const float t : {0.0F, 0.25F, 0.4F, 0.75F, 1.0F}) {
            CHECK(ease::apply(c.curve, t) == c.function(t));
        }
    }
}

TEST_CASE("rules call easing curves and apply", "[runtime][stdlib-easing]") {
    World world(1);
    const auto& s = world.registry.get<game::Samples>(World::slots().Probe);
    CHECK(s.out_quad_half == 0.75F);
    CHECK(s.in_quad_half == 0.25F);
    CHECK(s.applied_out_back == ease::out_back(0.4F));
    CHECK(s.applied_linear_past_end == 1.0F);
}

TEST_CASE("a stored curve shapes each entity's fade", "[runtime][stdlib-easing]") {
    World world(30);

    CHECK(world.fade(World::slots().Plain).curve == Curve::Linear);
    CHECK(world.fade(World::slots().Popped).curve == Curve::OutBack);
    CHECK(world.fade(World::slots().Retuned).curve == Curve::InQuad);

    CHECK(world.fade(World::slots().Plain).shade.r == Approx(0.5F));
    CHECK(world.fade(World::slots().Popped).shade.r == Approx(ease::out_back(0.5F)));
    CHECK(world.fade(World::slots().Retuned).shade.r == Approx(0.25F));
    CHECK(world.fade(World::slots().Popped).shade.a == 1.0F);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
