// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "perf_benchmark_support.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>

namespace {

namespace game = cactus_gen::perf_keep_chain;

constexpr int kSize       = 60;
constexpr int kDoubleSize = 85;  // about twice the tiles of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kKeepRunsPerFrame = 3;
// Round 1 runs the relation keeper, round 2 the last trait keeper, round 3 applies its result.
constexpr std::uint64_t kCommitRoundsPerFrame = 3;
// The two trait keepers sort their recipients into creation order.
constexpr std::uint64_t kOrderingsPerFrame = 4;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 1.5, .growth = 2.6};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_keep_chain.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::size_t dark() {
        return registry.view<game::Dark>().size();
    }

    [[nodiscard]] std::size_t near_rows() {
        return cactus::runtime::entt_backend::relation_store<game::Near>(registry).size();
    }
};

// Unlit tiles whose right neighbor is unlit too, or that have none.
[[nodiscard]] constexpr std::size_t dark_tiles(int size) {
    std::size_t columns = 0;
    for (int x = 0; x < size; ++x) {
        const bool lit       = x % 3 == 0;
        const bool lit_right = x + 1 < size && (x + 1) % 3 == 0;
        columns += !lit && !lit_right ? 1 : 0;
    }
    return columns * static_cast<std::size_t>(size);
}

[[nodiscard]] constexpr std::size_t near_rows(int size) {
    std::size_t columns = 0;
    for (int x = 0; x + 1 < size; ++x) {
        columns += (x + 1) % 3 == 0 ? 1 : 0;
    }
    return columns * static_cast<std::size_t>(size);
}

}  // namespace

TEST_CASE("perf: a keep chain settles in one frame and runs each keeper once", "[perf][keep-chain]") {
    World world(kSize);
    CHECK(world.near_rows() == near_rows(kSize));
    CHECK(world.dark() == dark_tiles(kSize));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.near_rows() == near_rows(kSize));
    CHECK(world.dark() == dark_tiles(kSize));
    CHECK(work.keep_runs == kKeepRunsPerFrame);
    CHECK(work.commit_rounds <= kCommitRoundsPerFrame);
    CHECK(work.orderings == kOrderingsPerFrame);
}

TEST_CASE("perf: a keep chain stays within its frame time", "[perf][keep-chain][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("keep chain frame, 60x60") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
