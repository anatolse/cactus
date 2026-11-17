// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "perf_benchmark_support.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>

#include <algorithm>
#include <cmath>

namespace {

namespace game    = cactus_gen::perf_sweep;
namespace physics = cactus::runtime::physics;
using Body        = cactus_gen::stdlib::physics::volume::CharacterBody;
using Transform   = cactus_gen::stdlib::transform::volume::WorldTransform;
using Solid       = cactus_gen::stdlib::physics::volume::Solid;

constexpr int kWorkSize    = 8;  // work is linear in bodies, and a small world keeps Debug runs short
constexpr int kSize        = 20;
constexpr int kDoubleSize  = 28;  // about twice the bodies of kSize
constexpr int kSettleTicks = 50;

// Work counts are exact at kWorkSize; times are O3 measurements at kSize x 1.5.
constexpr std::uint64_t kCandidatesPerFrame      = 960;
constexpr std::uint64_t kFreeCandidatesPerFrame  = 192;
// A body stopped in an acute crease plans no step lift, so its floor tile stays a candidate of each slide pass.
constexpr std::uint64_t kAcuteCandidatesPerFrame = 1152;
constexpr std::uint64_t kCommandsPerFrame        = 0;
// The slide rule sweeps every body once per pass, so this is its sweeps per body per tick.
constexpr std::uint64_t kSlidePassesPerFrame = 3;
constexpr float kAcuteHalfAngle              = 20.0F;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 8.5, .growth = 2.6};

struct World {
    entt::registry registry;

    explicit World(int size, float half_angle = 45.0F, bool walls = true) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            auto& crowd      = reg.get<game::Crowd>(cactus_gen::detail::generated_named_slots().perf_sweep.Pen);
            crowd.size       = size;
            crowd.half_angle = half_angle;
            crowd.walls      = walls;
        });
        for (int i = 0; i < kSettleTicks; ++i) {
            tick();
        }
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    // Deepest overlap of any body with any solid, as a negative gap.
    [[nodiscard]] float deepest_gap() {
        float deepest = 0.0F;
        for (const auto body : registry.view<Body>()) {
            const auto body_shape = cactus_gen::detail::cactus_collider_shape_of(registry, body);
            for (const auto solid : registry.view<Solid>()) {
                const auto solid_shape = cactus_gen::detail::cactus_collider_shape_of(registry, solid);
                const auto body_bounds  = physics::bounds(*body_shape);
                const auto solid_bounds = physics::bounds(*solid_shape);
                if (!cactus_perf::backend::proxy_bounds_overlap(body_bounds.min, body_bounds.max, solid_bounds.min, solid_bounds.max)) {
                    continue;
                }
                deepest = std::min(deepest, physics::separation(*body_shape, *solid_shape).distance);
            }
        }
        return deepest;
    }

    // Whether every body has stopped moving and plans no step lift.
    [[nodiscard]] bool all_stopped() {
        for (const auto body : registry.view<Body>()) {
            const auto& state = registry.get<Body>(body);
            if (state.velocity.x != 0.0F || state.velocity.z != 0.0F || state.lift != 0.0F) {
                return false;
            }
        }
        return true;
    }

    // Whether every body rests inside its wedge, short of the apex.
    [[nodiscard]] bool all_wedged() {
        for (const auto body : registry.view<Body>()) {
            const Vector3 position = registry.get<Transform>(body).position;
            const float cell_x     = std::round(position.x / game::SPACING) * game::SPACING;
            if (position.x < cell_x + 1.0F || position.x > cell_x + 3.0F) {
                return false;
            }
        }
        return true;
    }
};

}  // namespace

TEST_CASE("perf: bodies pressed into wedges sweep and slide without overlap", "[perf][sweep]") {
    World world(kWorkSize);
    REQUIRE(world.registry.view<Body>().size() == static_cast<std::size_t>(kWorkSize * kWorkSize));
    REQUIRE(world.all_wedged());
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.all_wedged());
    CHECK(world.deepest_gap() >= -physics::kSkin);
    CHECK(work.candidates <= kCandidatesPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
    CHECK(work.repeat_passes == kSlidePassesPerFrame);
}

TEST_CASE("perf: freely moving bodies sweep three times per tick", "[perf][sweep]") {
    World world(kWorkSize, 45.0F, false);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.deepest_gap() >= -physics::kSkin);
    CHECK(work.candidates <= kFreeCandidatesPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
    CHECK(work.repeat_passes == kSlidePassesPerFrame);
}

TEST_CASE("perf: bodies pressed into acute wedges sweep three times per tick", "[perf][sweep]") {
    World world(kWorkSize, kAcuteHalfAngle);
    REQUIRE(world.all_wedged());
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.all_wedged());
    CHECK(world.all_stopped());
    CHECK(world.deepest_gap() >= -physics::kSkin);
    CHECK(work.candidates <= kAcuteCandidatesPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
    CHECK(work.repeat_passes == kSlidePassesPerFrame);
}

TEST_CASE("perf: bodies pressed into wedges stay within their frame time", "[perf][sweep][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("wedged bodies frame, 20x20") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
