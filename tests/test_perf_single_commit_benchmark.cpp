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

namespace game = cactus_gen::perf_single_commit;

constexpr int kSize       = 60;
constexpr int kDoubleSize = 85;  // about twice the bodies of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kCommandsPerFrame       = (2 * kSize * kSize) + 2;
constexpr std::uint64_t kCarrierLookupsPerFrame = 1;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 4.0, .growth = 3.0};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_single_commit.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }
};

}  // namespace

TEST_CASE("perf: replacing a singleton looks up its carrier once per round", "[perf][single-commit]") {
    World world(kSize);
    REQUIRE(world.registry.view<game::Mover>().size() == static_cast<std::size_t>(kSize * kSize));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.commands == kCommandsPerFrame);
    CHECK(work.carrier_lookups == kCarrierLookupsPerFrame);
    CHECK(world.registry.view<game::Terrain>().size() == 1);
    CHECK(cactus::runtime::entt_backend::runtime_fault_count() == 0);
}

TEST_CASE("perf: commit rounds with a singleton stay within their frame time", "[perf][single-commit][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("single commit frame, 60x60") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
