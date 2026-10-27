// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

namespace backend = cactus::runtime::entt_backend;
namespace fake    = cactus_raylib_fake;

using HiddenIn = cactus_gen::stdlib::render::visibility::HiddenIn;
using HideLeft  = cactus_gen::per_view_visibility::HideLeft;
using TextLabel = cactus_gen::stdlib::render::text::TextLabel;

constexpr Vector3 kHiddenBlock{.x = -2.0F, .y = 0.0F, .z = 0.0F};
constexpr Vector3 kShownBlock{.x = 2.0F, .y = 0.0F, .z = 0.0F};
constexpr Vector3 kModel{.x = 0.0F, .y = 0.0F, .z = -3.0F};
// The one-cell volume shows all six faces in each view that draws it.
constexpr int kCubeFaces = 6;

// One frame's mesh draws in each viewport; the screen is 800 pixels wide.
struct ViewDraws {
    std::vector<Matrix> left  = fake::mesh_draws_in_scissor(0);
    std::vector<Matrix> right = fake::mesh_draws_in_scissor(400);
};

struct World {
    entt::registry registry;

    explicit World(bool window_ready = true) {
        fake::reset();
        fake::set_window_ready(window_ready);
        fake::set_screen_size(800, 400);
        backend::release_render_resources();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
        frame(3);
    }

    void frame(int count = 1) {
        for (int i = 0; i < count; ++i) {
            fake::clear_call_log();
            backend::generated_inject_external_event(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
            backend::generated_drain_external_events(registry);
        }
    }

    [[nodiscard]] std::size_t rows() {
        return backend::relation_store<HiddenIn>(registry).size();
    }

    static entt::entity left() {
        return cactus_gen::detail::generated_named_slots().per_view_visibility.Left;
    }

    static const backend::RenderDebugState& debug() {
        return backend::render_debug_state();
    }
};

}  // namespace

TEST_CASE("per-view visibility: a hidden entity is drawn in the other viewport only",
          "[runtime][codegen-entt][stdlib-render][visibility]") {
    World world;
    CHECK(world.rows() == 4);
    world.frame();
    const ViewDraws draws;
    CHECK_FALSE(fake::has_draw_at(draws.left, kHiddenBlock));
    CHECK(fake::has_draw_at(draws.right, kHiddenBlock));
    CHECK(fake::has_draw_at(draws.left, kShownBlock));
    CHECK(fake::has_draw_at(draws.right, kShownBlock));
    CHECK_FALSE(fake::has_draw_at(draws.left, kModel));
    CHECK(fake::has_draw_at(draws.right, kModel));
    CHECK(World::debug().voxel_faces_drawn == kCubeFaces);
}

TEST_CASE("per-view visibility: with no rows every viewport draws everything",
          "[runtime][codegen-entt][stdlib-render][visibility]") {
    World world;
    world.registry.clear<HideLeft>();
    world.frame(2);
    CHECK(world.rows() == 0);
    const ViewDraws draws;
    for (const auto position : {kHiddenBlock, kShownBlock, kModel}) {
        CHECK(fake::has_draw_at(draws.left, position));
        CHECK(fake::has_draw_at(draws.right, position));
    }
    CHECK(World::debug().voxel_faces_drawn == 2 * kCubeFaces);
}

TEST_CASE("per-view visibility: a hidden 3D label is submitted for the other viewport only",
          "[runtime][codegen-entt][stdlib-render][visibility]") {
    World world(false);
    world.registry.get<TextLabel>(cactus_gen::detail::generated_named_slots().per_view_visibility.Label).visible = true;
    const auto submitted = [&] {
        const int before = World::debug().submitted_texts_3d;
        world.frame();
        return World::debug().submitted_texts_3d - before;
    };
    CHECK(submitted() == 1);
    world.registry.clear<HideLeft>();
    world.frame();
    CHECK(submitted() == 2);
}

TEST_CASE("per-view visibility: destroying a viewport removes its rows",
          "[runtime][codegen-entt][stdlib-render][visibility]") {
    World world;
    REQUIRE(world.rows() == 4);
    world.registry.destroy(World::left());
    CHECK(world.rows() == 0);
    world.frame();
    const ViewDraws draws;
    CHECK(draws.left.empty());
    CHECK(fake::has_draw_at(draws.right, kHiddenBlock));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
