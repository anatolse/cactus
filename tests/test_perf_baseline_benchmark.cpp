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

namespace game = cactus_gen::perf_baseline;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the movers of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kCommandsPerFrame = 0;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 0.02, .growth = 2.6};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_baseline.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }
};

}  // namespace

TEST_CASE("perf: a per-entity update queues no structural work", "[perf][baseline]") {
    World world(kSize);
    REQUIRE(world.registry.view<game::Mover>().size() == static_cast<std::size_t>(kSize * kSize));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.commands <= kCommandsPerFrame);
    CHECK(work.orderings == 0);
}

TEST_CASE("perf: a per-entity update stays within its frame time", "[perf][baseline][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("per-entity update frame, 85x85") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
