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

namespace game = cactus_gen::perf_clamped;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the meters of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kMeters = static_cast<std::uint64_t>(kSize) * kSize;
// Gain and Drain each add once to each of the three fields of every meter.
constexpr std::uint64_t kMergesPerFrame = kMeters * 6;
// One pending sum per field and meter, settled when the activation ends.
constexpr std::uint64_t kSettlesPerFrame = kMeters * 3;
constexpr std::uint64_t kCommandsPerFrame = 0;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 2.3, .growth = 2.6};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_clamped.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }
};

}  // namespace

TEST_CASE("perf: clamped fields merge each delta once and settle each field once", "[perf][clamped]") {
    World world(kSize);
    REQUIRE(world.registry.view<game::Meter>().size() == kMeters);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.commands <= kCommandsPerFrame);
    CHECK(work.clamp_merges == kMergesPerFrame);
    CHECK(work.clamp_settles == kSettlesPerFrame);
    const auto& meter = world.registry.get<game::Meter>(world.registry.view<game::Meter>().front());
    CHECK(meter.seen == 100.0F);
    CHECK(meter.charge == 998);
}

TEST_CASE("perf: clamped fields stay within their frame time", "[perf][clamped][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("clamped meters frame, 85x85") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
