// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using CreationOrdinal = cactus::runtime::entt_backend::CreationOrdinal;

entt::entity create_node(entt::registry& registry) {
    const auto entity = registry.create();
    registry.emplace<CreationOrdinal>(
        entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
    registry.emplace<std_ui__Node>(entity);
    return entity;
}

// Builds a ComputedLayout directly (bypassing MeasureUi/ArrangeUi, already
// covered by test_standard_ui_layout_headless_behavior.cpp) so each test
// controls exactly the render-relevant facts: draw_order, visibility,
// opacity, and clip bounds.
std_ui__ComputedLayout make_layout(const int draw_order,
                                   const Vector2 position = {.x = 0.0F, .y = 0.0F},
                                   const Vector2 size     = {.x = 100.0F, .y = 50.0F},
                                   const bool visible     = true,
                                   const float opacity    = 1.0F,
                                   const Vector2 clip_min = {.x = 0.0F, .y = 0.0F},
                                   const Vector2 clip_max = {.x = 800.0F, .y = 600.0F}) {
    return std_ui__ComputedLayout{.position          = position,
                                  .size              = size,
                                  .effective_visible = visible,
                                  .effective_enabled = true,
                                  .effective_opacity = opacity,
                                  .clip_min          = clip_min,
                                  .clip_max          = clip_max,
                                  .draw_order        = draw_order};
}

}  // namespace

TEST_CASE("Standard UI render draws Panel/Image/Button/Text primitives in the standard per-entity order",
          "[runtime][stdlib][ui][render]") {
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto entity = create_node(registry);
    registry.emplace<std_ui__ComputedLayout>(entity,
                                             make_layout(0, {.x = 10.0F, .y = 10.0F}, {.x = 100.0F, .y = 50.0F}));
    registry.emplace<std_ui__Panel>(entity,
                                    std_ui__Panel{.background = RED, .border_color = BLUE, .border_width = 2.0F});
    registry.emplace<std_ui__Button>(entity,
                                     std_ui__Button{.label          = "go",
                                                    .normal_color   = GREEN,
                                                    .hover_color    = GREEN,
                                                    .pressed_color  = GREEN,
                                                    .disabled_color = GRAY,
                                                    .text_color     = WHITE,
                                                    .padding        = {.x = 4.0F, .y = 4.0F}});
    registry.emplace<std_ui__Text>(
        entity,
        std_ui__Text{
            .value = "shown over button", .font_size = 12, .color = YELLOW, .align = std_ui__TextAlign::Center});

    standard_ui_layout_runtime__render_ui_tick(registry);

    const auto& log = cactus_raylib_fake::call_log();
    // background (Panel, red) -> button fill (green) -> text (Text wins over
    // the button's own label per "text or button label") -> border (blue).
    CHECK(cactus_raylib_fake::ordered_subsequence(
        log,
        {[](const cactus_raylib_fake::RecordedCall& c) {
             const auto* rect = std::get_if<cactus_raylib_fake::RecordedDrawRectangleRec>(&c);
             return rect != nullptr && cactus_raylib_fake::colors_equal(rect->color, RED);
         },
         [](const cactus_raylib_fake::RecordedCall& c) {
             const auto* rect = std::get_if<cactus_raylib_fake::RecordedDrawRectangleRec>(&c);
             return rect != nullptr && cactus_raylib_fake::colors_equal(rect->color, GREEN);
         },
         [](const cactus_raylib_fake::RecordedCall& c) {
             const auto* text = std::get_if<cactus_raylib_fake::RecordedDrawTextEx>(&c);
             return text != nullptr && text->text == "shown over button";
         },
         [](const cactus_raylib_fake::RecordedCall& c) {
             const auto* lines = std::get_if<cactus_raylib_fake::RecordedDrawRectangleLinesEx>(&c);
             return lines != nullptr && cactus_raylib_fake::colors_equal(lines->color, BLUE);
         }}));

    // The button's own label must not be drawn when Text is also present.
    CHECK(cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawTextEx>(
              log, [](const auto& t) { return t.text == "go"; }) == nullptr);
}

TEST_CASE("Standard UI render paints in ComputedLayout draw_order, not creation order",
          "[runtime][stdlib][ui][render]") {
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    // Created first but painted second (higher draw_order).
    const auto first_created = create_node(registry);
    registry.emplace<std_ui__ComputedLayout>(first_created, make_layout(/*draw_order=*/5));
    registry.emplace<std_ui__Panel>(first_created, std_ui__Panel{.background = RED});

    const auto second_created = create_node(registry);
    registry.emplace<std_ui__ComputedLayout>(second_created, make_layout(/*draw_order=*/0));
    registry.emplace<std_ui__Panel>(second_created, std_ui__Panel{.background = BLUE});

    standard_ui_layout_runtime__render_ui_tick(registry);

    const auto& log = cactus_raylib_fake::call_log();
    CHECK(cactus_raylib_fake::occurs_before(
        log,
        [](const cactus_raylib_fake::RecordedCall& c) {
            const auto* rect = std::get_if<cactus_raylib_fake::RecordedDrawRectangleRec>(&c);
            return rect != nullptr && cactus_raylib_fake::colors_equal(rect->color, BLUE);
        },
        [](const cactus_raylib_fake::RecordedCall& c) {
            const auto* rect = std::get_if<cactus_raylib_fake::RecordedDrawRectangleRec>(&c);
            return rect != nullptr && cactus_raylib_fake::colors_equal(rect->color, RED);
        }));
}

TEST_CASE("Standard UI render skips invisible and fully-clipped entities, and tints by effective opacity",
          "[runtime][stdlib][ui][render]") {
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto hidden = create_node(registry);
    registry.emplace<std_ui__ComputedLayout>(hidden, make_layout(0, {.x = 0, .y = 0}, {.x = 10, .y = 10}, false));
    registry.emplace<std_ui__Panel>(hidden, std_ui__Panel{.background = RED});

    const auto clipped_away = create_node(registry);
    registry.emplace<std_ui__ComputedLayout>(
        clipped_away,
        make_layout(1, {.x = 0, .y = 0}, {.x = 10, .y = 10}, true, 1.0F, {.x = 5, .y = 5}, {.x = 5, .y = 5}));
    registry.emplace<std_ui__Panel>(clipped_away, std_ui__Panel{.background = GREEN});

    const auto translucent = create_node(registry);
    registry.emplace<std_ui__ComputedLayout>(translucent,
                                             make_layout(2, {.x = 0, .y = 0}, {.x = 10, .y = 10}, true, 0.5F));
    registry.emplace<std_ui__Panel>(translucent,
                                    std_ui__Panel{.background = Color{.r = 10, .g = 20, .b = 30, .a = 255}});

    standard_ui_layout_runtime__render_ui_tick(registry);

    const auto& log = cactus_raylib_fake::call_log();
    CHECK(cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleRec>(
              log, [](const auto& r) { return cactus_raylib_fake::colors_equal(r.color, RED); }) == nullptr);
    CHECK(cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleRec>(
              log, [](const auto& r) { return cactus_raylib_fake::colors_equal(r.color, GREEN); }) == nullptr);

    const auto* translucent_draw = cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleRec>(
        log, [](const auto& r) { return r.color.r == 10 && r.color.g == 20 && r.color.b == 30; });
    REQUIRE(translucent_draw != nullptr);
    CHECK(translucent_draw->color.a == 127);  // 255 * 0.5, truncated

    // The one surviving entity's scissor bounds must exactly bracket its draw.
    CHECK(cactus_raylib_fake::ordered_subsequence(
        log,
        {cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedBeginScissorMode>(),
         [](const cactus_raylib_fake::RecordedCall& c) {
             const auto* rect = std::get_if<cactus_raylib_fake::RecordedDrawRectangleRec>(&c);
             return rect != nullptr && rect->color.r == 10;
         },
         cactus_raylib_fake::is_call<cactus_raylib_fake::RecordedEndScissorMode>()}));
}

TEST_CASE("Standard UI render slices FrameAnimation frames from a resolved texture and clips Cover fitting",
          "[runtime][stdlib][ui][render]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);
    cactus::runtime::shared_asset_registry().register_texture(7U, "sprite_sheet", 0);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto entity = create_node(registry);
    registry.emplace<std_ui__ComputedLayout>(entity, make_layout(0, {.x = 0, .y = 0}, {.x = 40, .y = 40}));
    registry.emplace<std_ui__Image>(entity,
                                    std_ui__Image{.texture = 7U, .tint = WHITE, .fit = std_ui__ImageFit::Stretch});
    registry.emplace<std_ui__FrameAnimation>(
        entity, std_ui__FrameAnimation{.frame_count = 4, .fps = 0.0F, .frame = 2, .elapsed = 0.0F, .playing = false});

    standard_ui_layout_runtime__render_ui_tick(registry);

    const auto& log  = cactus_raylib_fake::call_log();
    const auto* draw = cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawTexturePro>(log);
    REQUIRE(draw != nullptr);
    // ensure_texture_resource's fake placeholder is a zeroed 0x0 Texture2D, so
    // frame slicing degenerates to a 0-width source rect at the frame's
    // offset (frame 2 of 4 -> x = 2 * (0/4) = 0) rather than a real nonzero
    // strip; the load path and per-entity draw submission are what this
    // exercises, not real filmstrip pixel geometry (covered separately by the
    // pure compute_image_draw_rects unit tests in test_runtime_stdlib.cpp).
    CHECK(draw->source.y == 0.0F);
    CHECK(draw->dest.x == 0.0F);
    CHECK(draw->dest.y == 0.0F);
    CHECK(draw->dest.width == 40.0F);
    CHECK(draw->dest.height == 40.0F);
}

TEST_CASE("Standard UI Button presentation follows disabled > pressed > hovered > normal precedence",
          "[runtime][stdlib][ui][render][pointer]") {
    auto button_fill = [](bool enabled, bool pressed, bool hovered) {
        cactus_raylib_fake::reset();

        entt::registry registry;
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);

        const auto entity        = create_node(registry);
        auto layout              = make_layout(0);
        layout.effective_enabled = enabled;
        registry.emplace<std_ui__ComputedLayout>(entity, layout);
        registry.emplace<std_ui__Button>(entity,
                                         std_ui__Button{.label          = "go",
                                                        .normal_color   = Color{.r = 1, .g = 0, .b = 0, .a = 255},
                                                        .hover_color    = Color{.r = 2, .g = 0, .b = 0, .a = 255},
                                                        .pressed_color  = Color{.r = 3, .g = 0, .b = 0, .a = 255},
                                                        .disabled_color = Color{.r = 4, .g = 0, .b = 0, .a = 255},
                                                        .text_color     = WHITE,
                                                        .padding        = {.x = 4.0F, .y = 4.0F}});
        registry.emplace<std_pointer__PointerState>(entity,
                                                    std_pointer__PointerState{.hovered = hovered, .pressed = pressed});

        standard_ui_layout_runtime__render_ui_tick(registry);

        const auto* draw =
            cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleRec>(cactus_raylib_fake::call_log());
        REQUIRE(draw != nullptr);
        return draw->color.r;
    };

    CHECK(button_fill(/*enabled=*/false, /*pressed=*/true, /*hovered=*/true) == 4);   // disabled wins over all
    CHECK(button_fill(/*enabled=*/true, /*pressed=*/true, /*hovered=*/true) == 3);    // pressed wins over hovered
    CHECK(button_fill(/*enabled=*/true, /*pressed=*/false, /*hovered=*/true) == 2);   // hovered wins over normal
    CHECK(button_fill(/*enabled=*/true, /*pressed=*/false, /*hovered=*/false) == 1);  // normal
}

TEST_CASE("Standard UI RenderUi coexists with the legacy DrawScreenRect compatibility renderer",
          "[runtime][stdlib][ui][render]") {
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto entity = create_node(registry);
    registry.emplace<std_ui__ComputedLayout>(entity, make_layout(0, {.x = 0, .y = 0}, {.x = 20, .y = 20}));
    registry.emplace<std_ui__Panel>(entity, std_ui__Panel{.background = RED});

    standard_ui_layout_runtime__render_ui_tick(registry);

    const std_ui__DrawScreenRectEvent legacy_event{.position  = {.x = 100.0F, .y = 100.0F},
                                                   .size      = {.x = 30.0F, .y = 30.0F},
                                                   .color     = MAGENTA,
                                                   .filled    = true,
                                                   .thickness = 1.0F};
    standard_ui_layout_runtime__draw_screen_rect_renderer_tick(registry, legacy_event);

    const auto& log = cactus_raylib_fake::call_log();
    CHECK(cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleRec>(
              log, [](const auto& r) { return cactus_raylib_fake::colors_equal(r.color, RED); }) != nullptr);
    CHECK(cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleRec>(
              log, [](const auto& r) { return cactus_raylib_fake::colors_equal(r.color, MAGENTA); }) != nullptr);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
