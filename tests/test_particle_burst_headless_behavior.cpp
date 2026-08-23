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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

namespace {

// examples/particle-burst/particle_burst.cactus: GRAVITY=300.0,
// PARTICLE_LIFETIME=0.9s, PARTICLE_COUNT=8, PARTICLE_SPEED=150.0. The
// render-pass rewrite (add-render-pass-phases) touches only rendering
// (ParticleVertex/ParticleFragment); spawn/gravity/lifetime simulation is
// byte-for-byte unchanged from the previously-shipped Shape-based version —
// these scenarios mirror example-particle-burst/spec.md's unmodified
// requirements to confirm that stayed true.
constexpr int kParticleCount   = 8;
constexpr float kParticleSpeed = 150.0F;
constexpr float kDt            = 1.0F / 60.0F;

void click_at(Vector2 position) {
    cactus_raylib_fake::set_mouse_position(position);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, true);
}

// "Pressed this frame" is a scripted flag, not an auto-clearing edge — it
// stays true across every subsequent driven frame until explicitly cleared,
// which would otherwise re-trigger EmitParticleBurst (and spawn another 8
// particles) on every following frame.
void release_click() {
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, false);
}

std::size_t particle_count(entt::registry& registry) {
    std::size_t count = 0;
    for (auto entity : registry.view<particle_burst__Particle>()) {
        (void)entity;
        ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("particle-burst headless: a click spawns PARTICLE_COUNT particles at the cursor position",
          "[example-behavior][particle-burst][render-passes]") {
    cactus_raylib_fake::reset();
    constexpr Vector2 kClickPosition{.x = 320.0F, .y = 240.0F};
    click_at(kClickPosition);

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kDt);

    // `on input:` and `on fixed_tick:` both fire within the one driven frame
    // (input -> fixed_tick* in the standard phase order), so by the time
    // drive_one_frame returns, up to one fixed_tick step (dt matches the
    // frame's) has already nudged position/velocity — margins below absorb
    // exactly that one step's worth of movement (~PARTICLE_SPEED*dt) and
    // gravity contribution (~GRAVITY*dt), not simulation drift this test
    // should otherwise be sensitive to.
    REQUIRE(particle_count(registry) == static_cast<std::size_t>(kParticleCount));
    for (auto entity : registry.view<particle_burst__Particle, std_transform_flat__WorldTransform>()) {
        const auto& transform = registry.get<std_transform_flat__WorldTransform>(entity);
        CHECK(cactus_raylib_fake::approx_equal(transform.position, kClickPosition, 10.0F));

        const auto& particle = registry.get<particle_burst__Particle>(entity);
        const float speed_sq = (particle.velocity.x * particle.velocity.x) + (particle.velocity.y * particle.velocity.y);
        CHECK(std::sqrt(speed_sq) == Catch::Approx(kParticleSpeed).margin(10.0));
    }
}

TEST_CASE("particle-burst headless: no click spawns nothing", "[example-behavior][particle-burst][render-passes]") {
    // Guards against a test that would pass regardless of whether the input
    // gate is wired up at all.
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kDt);

    CHECK(particle_count(registry) == 0);
}

TEST_CASE("particle-burst headless: gravity pulls particles downward and they die after PARTICLE_LIFETIME",
          "[example-behavior][particle-burst][render-passes]") {
    cactus_raylib_fake::reset();
    constexpr Vector2 kClickPosition{.x = 320.0F, .y = 240.0F};
    click_at(kClickPosition);

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, kDt);
    release_click();
    REQUIRE(particle_count(registry) == static_cast<std::size_t>(kParticleCount));

    float initial_velocity_y = 0.0F;
    for (auto entity : registry.view<particle_burst__Particle>()) {
        initial_velocity_y = registry.get<particle_burst__Particle>(entity).velocity.y;
        break;
    }

    // A handful of fixed-step ticks later, gravity must have measurably
    // increased every surviving particle's downward velocity.
    constexpr int kGravityCheckFrames = 10;
    for (int frame = 0; frame < kGravityCheckFrames; ++frame) {
        cactus_headless_test::drive_frame(registry, kDt);
    }
    REQUIRE(particle_count(registry) == static_cast<std::size_t>(kParticleCount));
    for (auto entity : registry.view<particle_burst__Particle>()) {
        CHECK(registry.get<particle_burst__Particle>(entity).velocity.y > initial_velocity_y);
    }

    // Enough additional simulated time that every particle's 0.9s lifetime
    // must have elapsed, regardless of fixed_tick's exact internal cadence
    // relative to the driven frame dt.
    constexpr int kTotalFrames = 300;  // 5 simulated seconds at 60 fps
    for (int frame = kGravityCheckFrames; frame < kTotalFrames; ++frame) {
        cactus_headless_test::drive_frame(registry, kDt);
    }
    CHECK(particle_count(registry) == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
