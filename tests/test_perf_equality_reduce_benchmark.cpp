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

namespace game = cactus_gen::perf_equality_reduce;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the tiles of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kCandidatesPerFrame = 1'033;
constexpr std::uint64_t kCommandsPerFrame   = 1;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 2.7, .growth = 2.6};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_equality_reduce.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::size_t expected_walkable() {
        std::size_t count = 0;
        for (const auto tile : registry.view<game::Tile>()) {
            if (registry.get<game::Tile>(tile).kind == game::Kind::Floor || registry.all_of<game::Bridge>(tile)) {
                ++count;
            }
        }
        return count;
    }
};

}  // namespace

TEST_CASE("perf: an equality-planned reduction folds only matching tuples", "[perf][equality-reduce]") {
    World world(kSize);
    REQUIRE(world.registry.view<game::Walkable>().size() == world.expected_walkable());
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.registry.view<game::Walkable>().size() == world.expected_walkable());
    CHECK(work.candidates <= kCandidatesPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
}

TEST_CASE("perf: an equality-planned reduction stays within its frame time", "[perf][equality-reduce][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("equality reduce frame, 85x85") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
