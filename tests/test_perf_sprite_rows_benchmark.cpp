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

namespace game = cactus_gen::perf_sprite_rows;

constexpr int kSize     = 5000;
constexpr int kDrawn    = 0;
constexpr int kExternal = 1;

// A frame of drawing-rule quads may cost at most this many frames of extern sprites.
constexpr double kRatioLimit = 1.5;

struct World {
    entt::registry registry;

    World(int size, int kind) {
        cactus_perf::load_sized(registry, [&](entt::registry& reg) {
            auto& board = reg.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_sprite_rows.Field);
            board.size  = size;
            board.kind  = kind;
        });
        tick();
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }
};

}  // namespace

TEST_CASE("perf: textured quads of a drawing rule take one draw call", "[perf][drawing-rule][sprites]") {
    World world(kSize, kDrawn);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.drawing_quads == static_cast<std::uint64_t>(kSize));
    CHECK(work.drawing_draw_calls == 1);
    CHECK(work.drawing_textures == 1);
}

TEST_CASE("perf: a drawing rule draws quads about as fast as the extern sprite renderer",
          "[perf][drawing-rule][sprites][time]") {
    cactus_perf::skip_unless_timed();
    World drawn(kSize, kDrawn);
    World external(kSize, kExternal);
    auto drawn_step    = [&] { drawn.tick(); };
    auto external_step = [&] { external.tick(); };
    const double drawn_ms    = cactus_perf::median_ms(drawn_step);
    const double external_ms = cactus_perf::median_ms(external_step);
    INFO("drawing rule " << drawn_ms << " ms, extern sprites " << external_ms << " ms, ratio "
                         << drawn_ms / external_ms);
    CHECK(drawn_ms <= external_ms * kRatioLimit);
    BENCHMARK("drawing-rule quads, 5000") {
        drawn.tick();
    };
    BENCHMARK("extern sprites, 5000") {
        external.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
