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

namespace game = cactus_gen::perf_drawing_rule;

constexpr int kSize       = 5000;
constexpr int kDoubleSize = 10000;

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kRowsPerFrame      = 2ULL * kSize;  // young and old sparks
constexpr std::uint64_t kQuadsPerFrame     = kSize;         // young sparks
constexpr std::uint64_t kOrderingsPerFrame = 1;
// Per drawn row: its position, its age and its anchor's position. Once per draw: the board's scale.
constexpr std::uint64_t kUniformsPerFrame = (3ULL * kQuadsPerFrame) + 1;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 1.3, .growth = 3.0};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_drawing_rule.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }
};

}  // namespace

TEST_CASE("perf: a drawing rule visits only its domain's rows and draws only those passing where:",
          "[perf][drawing-rule]") {
    World world(kSize);
    REQUIRE(world.registry.view<game::Particle>().size() == static_cast<std::size_t>(3 * kSize));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.drawing_rows == kRowsPerFrame);
    CHECK(work.drawing_quads == kQuadsPerFrame);
    CHECK(work.drawing_uniforms == kUniformsPerFrame);
    CHECK(work.orderings == kOrderingsPerFrame);
}

TEST_CASE("perf: a drawing rule stays within its frame time", "[perf][drawing-rule][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("drawing rule frame, 5000 per kind") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
