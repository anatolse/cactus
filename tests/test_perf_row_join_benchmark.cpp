// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "perf_benchmark_support.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>

#include <map>

namespace {

namespace game    = cactus_gen::perf_row_join;
namespace backend = cactus::runtime::entt_backend;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the tiles of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kCandidatesPerFrame = 28'560;
constexpr std::uint64_t kCommandsPerFrame   = 1;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 5.0, .growth = 2.6};

// Every tile walks to its four edge neighbors; the west half belongs to Red,
// the east half to Blue.
struct World {
    entt::registry registry;
    std::size_t expected = 0;

    explicit World(int size) {
        const auto& slots = cactus_gen::detail::generated_named_slots().perf_row_join;
        cactus_perf::load_sized(registry, [&](entt::registry& reg) { reg.get<game::Board>(slots.Field).size = size; });
        std::map<std::pair<int, int>, entt::entity> at;
        for (const auto tile : cactus_perf::oldest_first<game::Tile>(registry)) {
            const auto& cell = registry.get<game::Tile>(tile);
            at.emplace(std::pair{cell.x, cell.z}, tile);
            backend::relation_store<game::OwnedBy>(registry).add(
                tile, cell.x < size / 2 ? slots.Red : slots.Blue, game::OwnedBy{});
        }
        auto& walk = backend::relation_store<game::Walk>(registry);
        for (const auto& [position, tile] : at) {
            const auto [x, z] = position;
            for (const auto& [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const auto next = at.find({x + dx, z + dz});
                if (next == at.end()) {
                    continue;
                }
                walk.add(tile, next->second, game::Walk{});
                expected += static_cast<std::size_t>((x < size / 2) == (x + dx < size / 2));
            }
        }
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::size_t territory() {
        return backend::relation_store<game::Territory>(registry).size();
    }
};

}  // namespace

TEST_CASE("perf: a two-row relation join keeps rows with bounded work", "[perf][row-join]") {
    World world(kSize);
    REQUIRE(world.territory() == world.expected);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.territory() == world.expected);
    CHECK(work.candidates <= kCandidatesPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
}

TEST_CASE("perf: a two-row relation join stays within its frame time", "[perf][row-join][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("row join frame, 85x85") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
