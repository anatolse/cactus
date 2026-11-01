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

namespace game = cactus_gen::perf_static_keep;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the tiles of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kCommandsPerFrame = 1;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 4.6, .growth = 2.6};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_static_keep.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::size_t rows() {
        return cactus::runtime::entt_backend::relation_store<game::Neighbor>(registry).size();
    }
};

[[nodiscard]] constexpr std::size_t edge_rows(int size) {
    return std::size_t{4} * static_cast<std::size_t>(size) * static_cast<std::size_t>(size - 1);
}

}  // namespace

TEST_CASE("perf: a static relation keep re-derives its rows with bounded work", "[perf][static-keep]") {
    World world(kSize);
    REQUIRE(world.rows() == edge_rows(kSize));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.rows() == edge_rows(kSize));
    CHECK(work.commands <= kCommandsPerFrame);
    CHECK(work.orderings == 0);
}

TEST_CASE("perf: a static relation keep stays within its frame time", "[perf][static-keep][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("static keep frame, 85x85") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
