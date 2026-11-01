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

namespace game    = cactus_gen::perf_union_fold;
namespace backend = cactus::runtime::entt_backend;

constexpr int kSize       = 45;
constexpr int kDoubleSize = 64;  // about twice the tiles of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::size_t kReachBuildsPerFrame = 2;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 0.84, .growth = 2.6};

// A territory that grows one tile per tick along a snake through the grid,
// starting at the first heart.
struct World {
    entt::registry registry;
    std::vector<entt::entity> path;
    std::size_t claimed = 0;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_union_fold.Field).size = size;
        });
        path = cactus_perf::oldest_first<game::Tile>(registry);
        std::ranges::sort(path, {}, [&](entt::entity tile) {
            const auto& cell = registry.get<game::Tile>(tile);
            return std::pair{cell.x, cell.x % 2 == 0 ? cell.z : size - 1 - cell.z};
        });
        while (claimed + 1 < path.size() / 4) {
            claim();
        }
        tick();
    }

    void claim() {
        backend::relation_store<game::Territory>(registry).add(path[claimed], path[claimed + 1], game::Territory{});
        ++claimed;
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    void grow() {
        if (claimed + 1 < path.size()) {
            claim();
        }
        tick();
    }

    [[nodiscard]] std::size_t builds() {
        return backend::derived_relation<game::Reach>(registry).builds();
    }

    [[nodiscard]] std::size_t connected() {
        return registry.view<game::Connected>().size();
    }
};

}  // namespace

TEST_CASE("perf: a union fold with a closure test searches once per heart", "[perf][union-fold]") {
    World world(kSize);
    // The claimed prefix of the path, plus the second heart, which only reaches itself.
    CHECK(world.connected() == world.claimed + 2);
    const auto builds_before = world.builds();
    const auto work          = cactus_perf::work_of([&] { world.grow(); });
    CHECK(world.connected() == world.claimed + 2);
    CHECK(world.builds() - builds_before <= kReachBuildsPerFrame);
    // Only identity tuples and Reach rows: each heart with itself, the first with its claimed path.
    CHECK(work.candidates == world.claimed + 2);
}

TEST_CASE("perf: a union fold stays within its frame time", "[perf][union-fold][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.grow(); }, [&] { twice.grow(); }, kTimes);
    BENCHMARK("union fold frame, 45x45") {
        base.grow();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
