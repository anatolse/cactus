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

namespace game = cactus_gen::perf_required_links;

constexpr int kSize       = 70;
constexpr int kDoubleSize = 99;  // about twice the hunters of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kLostPerFrame         = 490;
constexpr std::uint64_t kLinkUpdatesPerFrame  = 980;
constexpr std::uint64_t kCommandsPerFrame     = 1'960;
constexpr std::uint64_t kCandidatesPerFrame   = 490;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 7.0, .growth = 3.0};

struct World {
    entt::registry registry;
    int size;

    explicit World(int board_size) : size(board_size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_required_links.Field).size =
                board_size;
        });
        // The first ticks link every hunter and reach the steady cull rate.
        for (int frame = 0; frame < 3; ++frame) {
            tick();
        }
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::size_t hunters() const {
        return static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    }

    [[nodiscard]] std::size_t chasing() {
        return registry.view<game::Hunt::Chase>().size();
    }
};

}  // namespace

TEST_CASE("perf: required links follow targets and lose only the culled ones", "[perf][required-links]") {
    World world(kSize);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.chasing() + kLostPerFrame == world.hunters());
    CHECK(work.lost_links == kLostPerFrame);
    CHECK(work.link_updates <= kLinkUpdatesPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
    CHECK(work.candidates <= kCandidatesPerFrame);
    CHECK(cactus::runtime::entt_backend::runtime_fault_count() == 0);
}

TEST_CASE("perf: required links stay within their frame time", "[perf][required-links][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("required links frame, 70x70 hunters") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
