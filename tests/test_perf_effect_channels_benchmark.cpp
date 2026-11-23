// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "perf_benchmark_support.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>

namespace {

namespace game    = cactus_gen::perf_effect_channels;
namespace effects = cactus_gen::stdlib::render::effects;

constexpr int kSize       = 2000;
constexpr int kDoubleSize = 4000;
constexpr int kBurstEvery = 4;
constexpr int kHit        = kSize / kBurstEvery;

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
// Fold rows: one per live channel row (shift, scale, tint, glow) of each hit target.
constexpr std::uint64_t kFoldRowsPerFrame = 4ULL * kHit;
constexpr std::uint64_t kCommandsPerFrame = 0;
// Most of the time is per target, not per effect: every fold visits every target's group (2.5 ms with no effects).
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 4.3, .growth = 3.0};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            auto& board       = reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_effect_channels.Field);
            board.size        = size;
            board.burst_every = kBurstEvery;
        });
        tick();
    }

    void tick() {
        cactus_raylib_fake::clear_call_log();
        cactus_headless_test::drive_frame(registry);
    }
};

}  // namespace

TEST_CASE("perf: effect folds evaluate one row per live channel", "[perf][effect-channels]") {
    World world(kSize);
    REQUIRE(world.registry.view<effects::Effect>().size() == static_cast<std::size_t>(3 * kHit));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.candidates == kFoldRowsPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
}

TEST_CASE("perf: effect folds stay within their frame time", "[perf][effect-channels][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("effect channels frame, 2000 targets") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
