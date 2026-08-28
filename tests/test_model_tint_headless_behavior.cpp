// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace {

bool same_color(const Color left, const Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

const cactus_raylib_fake::RecordedDrawMesh* find_draw_at(const Vector3 position) {
    const auto& log = cactus_raylib_fake::call_log();
    const auto it   = std::ranges::find_if(log, [&](const auto& entry) {
        const auto* draw = std::get_if<cactus_raylib_fake::RecordedDrawMesh>(&entry);
        return draw != nullptr && cactus_raylib_fake::approx_equal(draw->transform, position);
    });
    if (it == log.end()) {
        return nullptr;
    }
    return std::get_if<cactus_raylib_fake::RecordedDrawMesh>(&*it);
}

}  // namespace

TEST_CASE("model tint headless: default, static, and animated submissions retain independent RGBA tints",
          "[runtime][codegen-entt][stdlib-models]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto renderers = registry.view<std_render_models__ModelRenderer>();
    REQUIRE(renderers.size() == 3);
    const bool has_default_white = std::ranges::any_of(renderers, [&](const auto entity) {
        return same_color(registry.get<std_render_models__ModelRenderer>(entity).color, WHITE);
    });
    CHECK(has_default_white);

    const auto& debug = cactus::runtime::entt_backend::render_debug_state();
    REQUIRE(debug.submitted_model_colors.size() == 3);
    CHECK(
        std::ranges::any_of(debug.submitted_model_colors, [](const Color color) { return same_color(color, WHITE); }));
    CHECK(std::ranges::any_of(debug.submitted_model_colors, [](const Color color) {
        return same_color(color, Color{.r = 128, .g = 64, .b = 32, .a = 128});
    }));
    CHECK(std::ranges::any_of(debug.submitted_model_colors, [](const Color color) {
        return same_color(color, Color{.r = 32, .g = 160, .b = 255, .a = 64});
    }));

    const auto* default_draw  = find_draw_at(Vector3{.x = -2.0F, .y = 0.0F, .z = 0.0F});
    const auto* static_draw   = find_draw_at(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    const auto* animated_draw = find_draw_at(Vector3{.x = 2.0F, .y = 0.0F, .z = 0.0F});
    REQUIRE(default_draw != nullptr);
    REQUIRE(static_draw != nullptr);
    REQUIRE(animated_draw != nullptr);
    CHECK(same_color(default_draw->diffuse_color, WHITE));
    CHECK(same_color(static_draw->diffuse_color, Color{.r = 128, .g = 64, .b = 32, .a = 128}));
    CHECK(same_color(animated_draw->diffuse_color, Color{.r = 32, .g = 160, .b = 255, .a = 64}));
}

TEST_CASE("model tint headless: drawing a tinted shared model does not leak into the next frame",
          "[runtime][codegen-entt][stdlib-models]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);

    const auto* default_draw = find_draw_at(Vector3{.x = -2.0F, .y = 0.0F, .z = 0.0F});
    REQUIRE(default_draw != nullptr);
    CHECK(same_color(default_draw->diffuse_color, WHITE));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
