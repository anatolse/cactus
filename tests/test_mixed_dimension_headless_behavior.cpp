// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

namespace backend = cactus::runtime::entt_backend;
namespace flat    = cactus_gen::stdlib::transform::flat;

auto& slots() {
    return cactus_gen::detail::generated_named_slots().mixed_dimension_runtime;
}

template <typename Recorded, typename Predicate>
bool drew(Predicate predicate) {
    return cactus_raylib_fake::find_call<Recorded>(cactus_raylib_fake::call_log(), predicate) != nullptr;
}

struct World {
    entt::registry registry;

    explicit World(bool window_ready = true) {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(window_ready);
        cactus_headless_test::init_and_load(registry);
        // The fake GL cannot bake 3D text, so the volume label shows only without a window.
        registry.get<cactus_gen::stdlib::render::text::TextLabel>(slots().VolumeLabel).visible = !window_ready;
        backend::reset_render_debug_state();
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] entt::entity hand() {
        for (const auto [entity, parent] : registry.view<const cactus_gen::stdlib::core::Parent>().each()) {
            if (parent.parent == slots().Arm) {
                return entity;
            }
        }
        return entt::null;
    }
};

}  // namespace

TEST_CASE("mixed dimensions: the sprite and the mesh each read their own Renderer",
          "[example-behavior][mixed-module-extern-rules]") {
    World world;
    CHECK(drew<cactus_raylib_fake::RecordedDrawTexturePro>([](const auto& call) {
        return cactus_raylib_fake::colors_equal(call.tint, Color{.r = 255, .g = 0, .b = 0, .a = 255});
    }));
    CHECK(drew<cactus_raylib_fake::RecordedDrawMesh>([](const auto& call) {
        return cactus_raylib_fake::colors_equal(call.diffuse_color, Color{.r = 0, .g = 0, .b = 255, .a = 255});
    }));
}

TEST_CASE("mixed dimensions: flat and volume text labels both draw", "[example-behavior][mixed-module-extern-rules]") {
    {
        World world;
        CHECK(drew<cactus_raylib_fake::RecordedDrawTextPro>([](const auto& call) { return call.text == "flat"; }));
    }
    World world(false);
    CHECK(backend::render_debug_state().submitted_texts_3d > 0);
}

TEST_CASE("mixed dimensions: a flat child composes onto its parent next to volume entities",
          "[example-behavior][mixed-module-extern-rules]") {
    World world;
    const auto hand = world.hand();
    REQUIRE(world.registry.valid(hand));
    const auto& position = world.registry.get<flat::WorldTransform>(hand).position;
    CHECK(position.x == 11.0F);
    CHECK(position.y == 0.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
