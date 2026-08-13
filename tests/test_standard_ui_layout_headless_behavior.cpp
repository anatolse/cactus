// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"

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

void set_parent(entt::registry& registry, entt::entity child, entt::entity parent) {
    registry.emplace_or_replace<std_core__Parent>(child, std_core__Parent{.parent = parent});
}

}  // namespace

TEST_CASE("Standard UI measures bottom-up and arranges top-down across edge-case trees",
          "[runtime][stdlib][ui][layout]") {
    // window_size()/text_size() are now backend-owned (ui_window_size/ui_text_size
    // in runtime.cpp) rather than test-supplied bare functions; script the fake's
    // window state instead of overriding a free function (task 7.1). text_size's
    // fake formula is length * font_size * 0.5 wide, font_size tall (see
    // fake_raylib.cpp's MeasureTextEx) — deterministic, not pixel-accurate.
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);
    cactus_raylib_fake::set_screen_size(640, 360);
    // texture_size() only reports a nonzero placeholder for a resolved asset
    // handle (ui_texture_size in runtime.cpp); register the handle this test
    // exercises so the Image entity below measures a real intrinsic size.
    cactus::runtime::shared_asset_registry().register_texture(40U, "test_texture", 0);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto root = create_node(registry);
    registry.emplace<std_ui__Stack>(
        root,
        std_ui__Stack{.axis = std_ui__Axis::Vertical, .gap = 3.0F, .align = std_ui__Align::Stretch, .padding = {2, 4}});
    registry.emplace<std_ui__Visual>(root, std_ui__Visual{.scale = {1, 1}, .opacity = 0.5F});

    const auto text = create_node(registry);
    set_parent(registry, text, root);
    registry.emplace<std_ui__Text>(
        text, std_ui__Text{.value = "abc", .font_size = 10, .color = WHITE, .align = std_ui__TextAlign::Start});
    registry.emplace<std_ui__Visual>(text, std_ui__Visual{.scale = {1, 1}, .opacity = 0.4F});

    const auto image = create_node(registry);
    set_parent(registry, image, root);
    registry.emplace<std_ui__Image>(image,
                                    std_ui__Image{.texture = 40U, .tint = WHITE, .fit = std_ui__ImageFit::Stretch});

    const auto hidden = create_node(registry);
    set_parent(registry, hidden, text);
    registry.get<std_ui__Node>(hidden).visible = false;

    const auto deep = create_node(registry);
    set_parent(registry, deep, hidden);
    registry.emplace<std_ui__PreferredSize>(deep, std_ui__PreferredSize{.min_size = {7, 9}});

    const auto empty = create_node(registry);
    registry.emplace<std_ui__Stack>(
        empty,
        std_ui__Stack{
            .axis = std_ui__Axis::Horizontal, .gap = 5.0F, .align = std_ui__Align::Center, .padding = {1, 2}});

    const auto horizontal = create_node(registry);
    registry.emplace<std_ui__Stack>(
        horizontal,
        std_ui__Stack{
            .axis = std_ui__Axis::Horizontal, .gap = -8.0F, .align = std_ui__Align::Center, .padding = {-2, 1}});
    const auto horizontal_first = create_node(registry);
    set_parent(registry, horizontal_first, horizontal);
    registry.emplace<std_ui__PreferredSize>(horizontal_first, std_ui__PreferredSize{.min_size = {5, 9}});
    const auto horizontal_second = create_node(registry);
    set_parent(registry, horizontal_second, horizontal);
    registry.emplace<std_ui__PreferredSize>(horizontal_second, std_ui__PreferredSize{.min_size = {7, 4}});

    const auto button = create_node(registry);
    registry.emplace<std_ui__Button>(button,
                                     std_ui__Button{.label          = "ok",
                                                    .normal_color   = GRAY,
                                                    .hover_color    = LIGHTGRAY,
                                                    .pressed_color  = DARKGRAY,
                                                    .disabled_color = BLACK,
                                                    .text_color     = WHITE,
                                                    .padding        = {-3, 2}});
    registry.emplace<std_ui__PreferredSize>(button, std_ui__PreferredSize{.min_size = {40, -5}});

    const auto overlay = create_node(registry);
    registry.emplace<std_ui__Overlay>(overlay, std_ui__Overlay{.padding = {-4, 2}});
    const auto overlay_small = create_node(registry);
    set_parent(registry, overlay_small, overlay);
    registry.emplace<std_ui__PreferredSize>(overlay_small, std_ui__PreferredSize{.min_size = {8, 3}});
    const auto overlay_large = create_node(registry);
    set_parent(registry, overlay_large, overlay);
    registry.emplace<std_ui__PreferredSize>(overlay_large, std_ui__PreferredSize{.min_size = {5, 11}});

    const auto implicit_overlay = create_node(registry);
    const auto implicit_child   = create_node(registry);
    set_parent(registry, implicit_child, implicit_overlay);
    registry.emplace<std_ui__PreferredSize>(implicit_child, std_ui__PreferredSize{.min_size = {13, 6}});

    const auto grid = create_node(registry);
    registry.emplace<std_ui__Grid>(
        grid, std_ui__Grid{.columns = 0, .cell_size = {-10, -20}, .gap = {-3, 2}, .padding = {1, -5}});
    const auto grid_automatic = create_node(registry);
    set_parent(registry, grid_automatic, grid);
    registry.emplace<std_ui__PreferredSize>(grid_automatic, std_ui__PreferredSize{.min_size = {12, 6}});
    const auto grid_explicit = create_node(registry);
    set_parent(registry, grid_explicit, grid);
    registry.emplace<std_ui__PreferredSize>(grid_explicit, std_ui__PreferredSize{.min_size = {30, 20}});
    registry.emplace<std_ui__GridItem>(grid_explicit,
                                       std_ui__GridItem{.column = 1, .row = 2, .column_span = 0, .row_span = 2});

    const auto precedence = create_node(registry);
    registry.emplace<std_ui__Stack>(
        precedence,
        std_ui__Stack{.axis = std_ui__Axis::Horizontal, .gap = 1.0F, .align = std_ui__Align::Start, .padding = {0, 0}});
    registry.emplace<std_ui__Grid>(
        precedence, std_ui__Grid{.columns = 9, .cell_size = {99, 99}, .gap = {99, 99}, .padding = {99, 99}});
    const auto precedence_child = create_node(registry);
    set_parent(registry, precedence_child, precedence);
    registry.emplace<std_ui__PreferredSize>(precedence_child, std_ui__PreferredSize{.min_size = {3, 4}});

    const auto anchor_root                                = create_node(registry);
    registry.get<std_ui__Node>(anchor_root).clip_children = true;
    registry.emplace<std_ui__Visual>(anchor_root, std_ui__Visual{.scale = {0.5F, 2.0F}, .opacity = 0.8F});
    const auto centered = create_node(registry);
    set_parent(registry, centered, anchor_root);
    registry.emplace<std_ui__PreferredSize>(centered, std_ui__PreferredSize{.min_size = {100, 50}});
    registry.emplace<std_ui__Anchors>(centered,
                                      std_ui__Anchors{.min        = {0.5F, 0.5F},
                                                      .max        = {0.5F, 0.5F},
                                                      .pivot      = {0.5F, 0.5F},
                                                      .offset     = {10, -5},
                                                      .margin_min = {0, 0},
                                                      .margin_max = {0, 0}});
    const auto stretched = create_node(registry);
    set_parent(registry, stretched, anchor_root);
    registry.emplace<std_ui__Anchors>(stretched,
                                      std_ui__Anchors{.min        = {0, 0},
                                                      .max        = {1, 1},
                                                      .pivot      = {0.9F, 0.1F},
                                                      .offset     = {0, 0},
                                                      .margin_min = {20, 30},
                                                      .margin_max = {20, 30}});

    const auto clipping_child = create_node(registry);
    set_parent(registry, clipping_child, anchor_root);
    registry.get<std_ui__Node>(clipping_child).clip_children = true;
    registry.emplace<std_ui__PreferredSize>(clipping_child, std_ui__PreferredSize{.min_size = {100, 100}});
    registry.emplace<std_ui__Anchors>(clipping_child,
                                      std_ui__Anchors{.min        = {0, 0},
                                                      .max        = {0, 0},
                                                      .pivot      = {0, 0},
                                                      .offset     = {600, 300},
                                                      .margin_min = {0, 0},
                                                      .margin_max = {0, 0}});
    const auto clipping_grandchild = create_node(registry);
    set_parent(registry, clipping_grandchild, clipping_child);
    registry.emplace<std_ui__Anchors>(clipping_grandchild,
                                      std_ui__Anchors{.min        = {0, 0},
                                                      .max        = {1, 1},
                                                      .pivot      = {0, 0},
                                                      .offset     = {0, 0},
                                                      .margin_min = {0, 0},
                                                      .margin_max = {0, 0}});

    const auto non_node_parent = registry.create();
    registry.emplace<CreationOrdinal>(
        non_node_parent, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
    const auto structural_root = create_node(registry);
    set_parent(registry, structural_root, non_node_parent);

    const auto stale_parent = registry.create();
    const auto stale_root   = create_node(registry);
    set_parent(registry, stale_root, stale_parent);
    registry.destroy(stale_parent);

    const auto atomic_root = create_node(registry);
    const auto atomic_a    = create_node(registry);
    set_parent(registry, atomic_a, atomic_root);
    registry.get<std_ui__Node>(atomic_a).z_index = 0;
    const auto atomic_a_high                     = create_node(registry);
    set_parent(registry, atomic_a_high, atomic_a);
    registry.get<std_ui__Node>(atomic_a_high).z_index = 1000;
    const auto atomic_b                               = create_node(registry);
    set_parent(registry, atomic_b, atomic_root);
    registry.get<std_ui__Node>(atomic_b).z_index = 1;

    // Sibling order must follow z_index, not creation order: reordered_first is
    // created first but carries the higher z_index, so it must paint after (and
    // therefore receive a larger draw_order than) reordered_second.
    const auto reorder_root  = create_node(registry);
    const auto reorder_first = create_node(registry);
    set_parent(registry, reorder_first, reorder_root);
    registry.get<std_ui__Node>(reorder_first).z_index = 5;
    const auto reorder_second                         = create_node(registry);
    set_parent(registry, reorder_second, reorder_root);
    registry.get<std_ui__Node>(reorder_second).z_index = 0;
    const auto reorder_tie_a                           = create_node(registry);
    set_parent(registry, reorder_tie_a, reorder_root);
    const auto reorder_tie_b = create_node(registry);
    set_parent(registry, reorder_tie_b, reorder_root);

    const cactus::runtime::entt_backend::std_core__inputPhaseRuntimeState input_measure_phase{};
    standard_ui_layout_runtime__measure_ui_std_core__input(registry, input_measure_phase);
    CHECK(registry.get<std_ui__DesiredSize>(grid).size.x == 62.0F);
    CHECK(registry.get<std_ui__DesiredSize>(grid).size.y == 42.0F);

    const cactus::runtime::entt_backend::std_core__late_tickPhaseRuntimeState measure_phase{};
    standard_ui_layout_runtime__measure_ui_std_core__late_tick(registry, measure_phase);
    standard_ui_layout_runtime__arrange_ui_std_core__input(registry, input_measure_phase);

    CHECK(registry.get<std_ui__DesiredSize>(text).size.x == 15.0F);
    CHECK(registry.get<std_ui__DesiredSize>(text).size.y == 10.0F);
    CHECK(registry.get<std_ui__DesiredSize>(image).size.x == 32.0F);
    CHECK(registry.get<std_ui__DesiredSize>(image).size.y == 32.0F);
    CHECK(registry.get<std_ui__DesiredSize>(root).size.x == 36.0F);
    CHECK(registry.get<std_ui__DesiredSize>(root).size.y == 53.0F);
    CHECK(registry.get<std_ui__DesiredSize>(empty).size.x == 2.0F);
    CHECK(registry.get<std_ui__DesiredSize>(empty).size.y == 4.0F);
    CHECK(registry.get<std_ui__DesiredSize>(deep).size.x == 7.0F);
    CHECK(registry.get<std_ui__DesiredSize>(deep).size.y == 9.0F);
    CHECK(registry.get<std_ui__DesiredSize>(horizontal).size.x == 12.0F);
    CHECK(registry.get<std_ui__DesiredSize>(horizontal).size.y == 11.0F);
    CHECK(registry.get<std_ui__DesiredSize>(button).size.x == 40.0F);
    CHECK(registry.get<std_ui__DesiredSize>(button).size.y == 20.0F);
    CHECK(registry.get<std_ui__DesiredSize>(overlay).size.x == 8.0F);
    CHECK(registry.get<std_ui__DesiredSize>(overlay).size.y == 15.0F);
    CHECK(registry.get<std_ui__DesiredSize>(implicit_overlay).size.x == 13.0F);
    CHECK(registry.get<std_ui__DesiredSize>(implicit_overlay).size.y == 6.0F);
    CHECK(registry.get<std_ui__DesiredSize>(grid).size.x == 62.0F);
    CHECK(registry.get<std_ui__DesiredSize>(grid).size.y == 42.0F);
    CHECK(registry.get<std_ui__DesiredSize>(precedence).size.x == 3.0F);
    CHECK(registry.get<std_ui__DesiredSize>(precedence).size.y == 4.0F);

    const auto& root_layout   = registry.get<std_ui__ComputedLayout>(root);
    const auto& text_layout   = registry.get<std_ui__ComputedLayout>(text);
    const auto& hidden_layout = registry.get<std_ui__ComputedLayout>(hidden);
    const auto& deep_layout   = registry.get<std_ui__ComputedLayout>(deep);
    CHECK(root_layout.size.x == 640.0F);
    CHECK(root_layout.size.y == 360.0F);
    CHECK(text_layout.effective_opacity == 0.2F);
    CHECK_FALSE(hidden_layout.effective_visible);
    CHECK_FALSE(deep_layout.effective_visible);
    CHECK(root_layout.draw_order < text_layout.draw_order);
    CHECK(text_layout.draw_order < hidden_layout.draw_order);
    CHECK(hidden_layout.draw_order < deep_layout.draw_order);

    const auto& centered_layout = registry.get<std_ui__ComputedLayout>(centered);
    CHECK(centered_layout.position.x == 280.0F);
    CHECK(centered_layout.position.y == 150.0F);
    CHECK(centered_layout.size.x == 100.0F);
    CHECK(centered_layout.size.y == 50.0F);
    const auto& stretched_layout = registry.get<std_ui__ComputedLayout>(stretched);
    CHECK(stretched_layout.position.x == 20.0F);
    CHECK(stretched_layout.position.y == 30.0F);
    CHECK(stretched_layout.size.x == 600.0F);
    CHECK(stretched_layout.size.y == 300.0F);
    CHECK(registry.get<std_ui__ComputedLayout>(anchor_root).size.x == 640.0F);
    CHECK(registry.get<std_ui__ComputedLayout>(anchor_root).size.y == 360.0F);
    CHECK(registry.get<std_ui__ComputedLayout>(centered).effective_opacity == 0.8F);

    const auto& nested_clip = registry.get<std_ui__ComputedLayout>(clipping_grandchild);
    CHECK(nested_clip.clip_min.x == 600.0F);
    CHECK(nested_clip.clip_min.y == 300.0F);
    CHECK(nested_clip.clip_max.x == 640.0F);
    CHECK(nested_clip.clip_max.y == 360.0F);

    CHECK(registry.get<std_ui__ComputedLayout>(structural_root).size.x == 640.0F);
    CHECK(registry.get<std_ui__ComputedLayout>(stale_root).size.y == 360.0F);

    CHECK(registry.get<std_ui__ComputedLayout>(atomic_a).draw_order <
          registry.get<std_ui__ComputedLayout>(atomic_a_high).draw_order);
    CHECK(registry.get<std_ui__ComputedLayout>(atomic_a_high).draw_order <
          registry.get<std_ui__ComputedLayout>(atomic_b).draw_order);

    // reorder_first was created before reorder_second but has the higher z_index,
    // so sibling stacking order must follow z_index rather than creation order.
    CHECK(registry.get<std_ui__ComputedLayout>(reorder_second).draw_order <
          registry.get<std_ui__ComputedLayout>(reorder_first).draw_order);
    // Equal z_index siblings fall back to stable creation ordinal.
    CHECK(registry.get<std_ui__ComputedLayout>(reorder_tie_a).draw_order <
          registry.get<std_ui__ComputedLayout>(reorder_tie_b).draw_order);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)