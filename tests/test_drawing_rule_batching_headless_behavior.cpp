// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

namespace {

namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        backend::release_render_resources();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
    }

    void frame() {
        backend::generated_inject_external_event(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
        backend::generated_drain_external_events(registry);
    }
};

struct Draws {
    int rows       = -1;
    int draw_calls = -1;
};

Draws draws_of(const std::string& rule) {
    const auto& all   = backend::render_debug_state().drawing_rule_draws;
    const auto found = std::ranges::find(all, rule, &backend::RenderDebugState::DrawingRuleDraws::rule);
    return found == all.end() ? Draws{} : Draws{.rows = found->rows, .draw_calls = found->draw_calls};
}

}  // namespace

TEST_CASE("drawing rule batching: a rule draws its rows in one call per texture run",
          "[example-behavior][drawing-rules][batching]") {
    World world;
    world.frame();
    world.frame();

    // No texture: one call, however many rows.
    CHECK(draws_of("Stack").rows == 2);
    CHECK(draws_of("Stack").draw_calls == 1);
    CHECK(draws_of("Glows").rows == 2);
    CHECK(draws_of("Glows").draw_calls == 1);
    // Tiles A, A, B, A: three runs.
    CHECK(draws_of("Tiles").rows == 4);
    CHECK(draws_of("Tiles").draw_calls == 3);
    // Only rows that pass where: are drawn.
    CHECK(draws_of("Dots").rows == 3);
    CHECK(draws_of("Dots").draw_calls == 1);
    // No drawn rows: no call.
    CHECK(draws_of("Ghosts").rows == 0);
    CHECK(draws_of("Ghosts").draw_calls == 0);
}

TEST_CASE("drawing rule batching: the counters count draw calls beside rows, quads and texture binds",
          "[example-behavior][drawing-rules][batching]") {
    World world;
    world.frame();
    const auto before = backend::perf_counters();
    world.frame();
    const auto& after = backend::perf_counters();
    // Stack 2, Mirrors 1, Codes 2, Glows 2, Tiles 4, Dots 6 visited, Ghosts 1 visited.
    CHECK(after.drawing_rows - before.drawing_rows == 18);
    CHECK(after.drawing_quads - before.drawing_quads == 14);
    CHECK(after.drawing_draw_calls - before.drawing_draw_calls == 8);
    CHECK(after.drawing_textures - before.drawing_textures == 3);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
