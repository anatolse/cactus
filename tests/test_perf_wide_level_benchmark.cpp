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

namespace game = cactus_gen::perf_wide_level;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the entities of kSize
constexpr int kRules      = 8;
constexpr int kWorkers    = 4;

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 1.6, .growth = 2.6};
// Four workers must at least halve the frame of the same build run on the main thread alone.
constexpr double kWorkerSpeedup = 2.0;

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_wide_level.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }
};

using cactus_headless_test::WorkerCount;

}  // namespace

TEST_CASE("perf: a wide level of disjoint rules runs as one batch of tasks", "[perf][wide_level]") {
    const WorkerCount workers(kWorkers);
    World world(kSize);
    REQUIRE(world.registry.view<game::A>().size() == static_cast<std::size_t>(kSize * kSize));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.commands == 0);
    CHECK(work.parallel_batches == 1);
    CHECK(work.parallel_tasks == kRules);
}

TEST_CASE("perf: a wide level of disjoint rules stays within its frame time", "[perf][wide_level][time]") {
    cactus_perf::skip_unless_timed();
    const WorkerCount workers(kWorkers);
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("wide level frame, 85x85, 4 workers") {
        base.tick();
    };
}

TEST_CASE("perf: workers shorten a wide level", "[perf][wide_level][time]") {
    cactus_perf::skip_unless_timed();
    World world(kSize);
    auto step = [&] { world.tick(); };
    double alone_ms = 0.0;
    {
        const WorkerCount none(0);
        alone_ms = cactus_perf::median_ms(step);
    }
    const WorkerCount workers(kWorkers);
    const double parallel_ms = cactus_perf::median_ms(step);
    INFO("main thread alone " << alone_ms << " ms, " << kWorkers << " workers " << parallel_ms << " ms");
    CHECK(alone_ms / parallel_ms >= kWorkerSpeedup);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
