// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "perf_benchmark_support.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>

#include <unordered_map>

namespace {

namespace game = cactus_gen::perf_reduced_keep;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the runners of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kCommandsPerFrame   = 1;
constexpr std::uint64_t kCandidatesPerFrame = 28'900;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 5.5, .growth = 3.0};

struct World {
    entt::registry registry;
    int size;

    explicit World(int board_size) : size(board_size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_reduced_keep.Field).size = board_size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::size_t runners() const {
        return static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    }

    // Each runner's kept gate.
    [[nodiscard]] std::unordered_map<entt::entity, entt::entity> winners() {
        std::unordered_map<entt::entity, entt::entity> result;
        for (const auto& entry : cactus::runtime::entt_backend::relation_store<game::AtGate>(registry).rows()) {
            result.emplace(entry.from, entry.to);
        }
        return result;
    }
};

// Rows whose winner differs between two frames.
[[nodiscard]] std::size_t changed_rows(const std::unordered_map<entt::entity, entt::entity>& before,
                                       const std::unordered_map<entt::entity, entt::entity>& after) {
    std::size_t changed = 0;
    for (const auto& [runner, gate] : after) {
        const auto found = before.find(runner);
        changed += found == before.end() || found->second != gate ? 1 : 0;
    }
    return changed;
}

}  // namespace

TEST_CASE("perf: a reduced relation keep replaces changed winners with bounded work", "[perf][reduced-keep]") {
    World world(kSize);
    const auto before = world.winners();
    REQUIRE(before.size() == world.runners());
    const auto work  = cactus_perf::work_of([&] { world.tick(); });
    const auto after = world.winners();
    CHECK(after.size() == world.runners());
    CHECK(changed_rows(before, after) == world.runners());
    CHECK(work.commands <= kCommandsPerFrame);
    CHECK(work.candidates <= kCandidatesPerFrame);
    CHECK(work.orderings == 0);
}

TEST_CASE("perf: a reduced relation keep stays within its frame time", "[perf][reduced-keep][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("reduced relation keep frame, 85x85 runners") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
