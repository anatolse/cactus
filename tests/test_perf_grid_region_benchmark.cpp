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

namespace game    = cactus_gen::perf_grid_region;
namespace backend = cactus::runtime::entt_backend;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the tiles of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kCandidatesPerFrame = 3'307;
constexpr std::uint64_t kCommandsPerFrame   = 1;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 0.47, .growth = 2.6};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_grid_region.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::size_t seen() {
        return backend::relation_store<game::Seen>(registry).size();
    }

    [[nodiscard]] std::size_t expected_seen() {
        std::size_t count = 0;
        for (const auto unit : registry.view<game::Unit>()) {
            const auto& pose = registry.get<game::Unit>(unit);
            for (const auto tile : registry.view<game::Tile>()) {
                const auto& cell = registry.get<game::Tile>(tile);
                const double dx  = pose.x - cell.x - 0.5;
                const double dz  = pose.z - cell.z - 0.5;
                if (dx * dx + dz * dz <= 36.0) {
                    ++count;
                }
            }
        }
        return count;
    }
};

}  // namespace

TEST_CASE("perf: a grid-region pair pass visits only the region's tiles", "[perf][grid-region]") {
    World world(kSize);
    REQUIRE(world.seen() == world.expected_seen());
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.seen() == world.expected_seen());
    CHECK(work.candidates <= kCandidatesPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
}

TEST_CASE("perf: a grid-region pair pass stays within its frame time", "[perf][grid-region][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("grid region frame, 85x85") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
