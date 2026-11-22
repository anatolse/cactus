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

namespace game   = cactus_gen::perf_color_fade;
namespace shapes = cactus_gen::stdlib::render::shapes;

constexpr int kSize       = 5000;
constexpr int kDoubleSize = 10000;
constexpr int kFlashEvery = 10;

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
constexpr std::uint64_t kConversionsPerFrame = kSize;  // one per drawn shape
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 1.5, .growth = 3.0};

// A color is four float channels; the shape trait carries one.
static_assert(sizeof(CactusColor) == 16);

struct World {
    entt::registry registry;

    explicit World(int size, int flash_every = 0) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            auto& board       = reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_color_fade.Field);
            board.size        = size;
            board.flash_every = flash_every;
        });
        tick();
    }

    // The fake GL logs every draw; an uncleared log makes frame times grow with the frame count.
    void tick() {
        cactus_raylib_fake::clear_call_log();
        cactus_headless_test::drive_frame(registry);
    }
};

}  // namespace

TEST_CASE("perf: a color blend and fade converts each drawn shape's color to bytes once per frame",
          "[perf][color]") {
    World world(kSize);
    REQUIRE(world.registry.view<shapes::Shape>().size() == static_cast<std::size_t>(kSize));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.color_conversions == kConversionsPerFrame);
    INFO("shape trait size: " << sizeof(shapes::Shape) << " bytes");
    CHECK(sizeof(shapes::Shape) >= sizeof(CactusColor));
}

TEST_CASE("perf: a drawn picture on every tenth shape still converts each color once", "[perf][color][picture]") {
    World world(kSize, kFlashEvery);
    REQUIRE(world.registry.view<game::Flash>().size() == static_cast<std::size_t>(kSize / kFlashEvery));
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.color_conversions == kConversionsPerFrame);
}

TEST_CASE("perf: a drawn picture on every tenth shape stays within its frame time", "[perf][color][picture][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize, kFlashEvery);
    World twice(kDoubleSize, kFlashEvery);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("color fade frame with pictures, 5000 shapes") {
        base.tick();
    };
}

TEST_CASE("perf: a color blend and fade stays within its frame time", "[perf][color][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("color fade frame, 5000 shapes") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
