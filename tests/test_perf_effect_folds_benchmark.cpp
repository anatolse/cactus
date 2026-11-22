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

namespace game = cactus_gen::perf_effect_folds;

constexpr int kSize       = 85;
constexpr int kDoubleSize = 120;  // about twice the bodies of kSize

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
// One candidate per folded row: four effects per body.
constexpr std::uint64_t kCandidatesPerFrame = 28'900;
constexpr std::uint64_t kCommandsPerFrame   = 0;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 6.5, .growth = 3.0};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_effect_folds.Field).size = size;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    // Every body's scale is its gains multiplied in creation order, and its flash the sum of four.
    [[nodiscard]] bool folded() {
        volatile float scale = 1.0F;
        for (int order = 0; order < 4; ++order) {
            const volatile float gain = 0.9F + (static_cast<float>(order) * 0.1F);
            scale                     = scale * gain;
        }
        const float red            = 4.0F * (32.0F / 255.0F);
        for (const auto& [entity, body] : registry.view<game::Body>().each()) {
            if (body.scale != scale || body.flash.r != red) {
                return false;
            }
        }
        return true;
    }
};

}  // namespace

TEST_CASE("perf: product and color sum fold each row once", "[perf][effect-folds]") {
    World world(kSize);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.folded());
    CHECK(work.candidates <= kCandidatesPerFrame);
    CHECK(work.commands <= kCommandsPerFrame);
}

TEST_CASE("perf: product and color sum stay within their frame time", "[perf][effect-folds][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("effect folds frame, 85x85") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
