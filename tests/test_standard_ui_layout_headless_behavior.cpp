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
    registry.emplace<cactus_gen::stdlib::ui::Node>(entity);
    return entity;
}

void set_parent(entt::registry& registry, entt::entity child, entt::entity parent) {
    registry.emplace_or_replace<cactus_gen::stdlib::core::Parent>(child,
                                                                  cactus_gen::stdlib::core::Parent{.parent = parent});
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
    registry.emplace<cactus_gen::stdlib::ui::Stack>(
        root,
        cactus_gen::stdlib::ui::Stack{.axis    = cactus_gen::stdlib::ui::Axis::Vertical,
                                      .gap     = 3.0F,
                                      .align   = cactus_gen::stdlib::ui::Align::Stretch,
                                      .padding = {2, 4}});
    registry.emplace<cactus_gen::stdlib::ui::Visual>(root,
                                                     cactus_gen::stdlib::ui::Visual{.scale = {1, 1}, .opacity = 0.5F});

    const auto text = create_node(registry);
    set_parent(registry, text, root);
    registry.emplace<cactus_gen::stdlib::ui::Text>(
        text,
        cactus_gen::stdlib::ui::Text{
            .value = "abc", .font_size = 10, .color = WHITE, .align = cactus_gen::stdlib::ui::TextAlign::Start});
    registry.emplace<cactus_gen::stdlib::ui::Visual>(text,
                                                     cactus_gen::stdlib::ui::Visual{.scale = {1, 1}, .opacity = 0.4F});

    const auto image = create_node(registry);
    set_parent(registry, image, root);
    registry.emplace<cactus_gen::stdlib::ui::Image>(
        image,
        cactus_gen::stdlib::ui::Image{.texture = 40U, .tint = WHITE, .fit = cactus_gen::stdlib::ui::ImageFit::Stretch});

    const auto hidden = create_node(registry);
    set_parent(registry, hidden, text);
    registry.get<cactus_gen::stdlib::ui::Node>(hidden).visible = false;

    const auto deep = create_node(registry);
    set_parent(registry, deep, hidden);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(deep,
                                                            cactus_gen::stdlib::ui::PreferredSize{.min_size = {7, 9}});

    const auto empty = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Stack>(
        empty,
        cactus_gen::stdlib::ui::Stack{.axis    = cactus_gen::stdlib::ui::Axis::Horizontal,
                                      .gap     = 5.0F,
                                      .align   = cactus_gen::stdlib::ui::Align::Center,
                                      .padding = {1, 2}});

    const auto horizontal = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Stack>(
        horizontal,
        cactus_gen::stdlib::ui::Stack{.axis    = cactus_gen::stdlib::ui::Axis::Horizontal,
                                      .gap     = -8.0F,
                                      .align   = cactus_gen::stdlib::ui::Align::Center,
                                      .padding = {-2, 1}});
    const auto horizontal_first = create_node(registry);
    set_parent(registry, horizontal_first, horizontal);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(horizontal_first,
                                                            cactus_gen::stdlib::ui::PreferredSize{.min_size = {5, 9}});
    const auto horizontal_second = create_node(registry);
    set_parent(registry, horizontal_second, horizontal);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(horizontal_second,
                                                            cactus_gen::stdlib::ui::PreferredSize{.min_size = {7, 4}});

    const auto button = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Button>(button,
                                                     cactus_gen::stdlib::ui::Button{.label          = "ok",
                                                                                    .normal_color   = GRAY,
                                                                                    .hover_color    = LIGHTGRAY,
                                                                                    .pressed_color  = DARKGRAY,
                                                                                    .disabled_color = BLACK,
                                                                                    .text_color     = WHITE,
                                                                                    .padding        = {-3, 2}});
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(
        button, cactus_gen::stdlib::ui::PreferredSize{.min_size = {40, -5}});

    const auto overlay = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Overlay>(overlay, cactus_gen::stdlib::ui::Overlay{.padding = {-4, 2}});
    const auto overlay_small = create_node(registry);
    set_parent(registry, overlay_small, overlay);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(overlay_small,
                                                            cactus_gen::stdlib::ui::PreferredSize{.min_size = {8, 3}});
    const auto overlay_large = create_node(registry);
    set_parent(registry, overlay_large, overlay);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(overlay_large,
                                                            cactus_gen::stdlib::ui::PreferredSize{.min_size = {5, 11}});

    const auto implicit_overlay = create_node(registry);
    const auto implicit_child   = create_node(registry);
    set_parent(registry, implicit_child, implicit_overlay);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(implicit_child,
                                                            cactus_gen::stdlib::ui::PreferredSize{.min_size = {13, 6}});

    const auto grid = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Grid>(
        grid, cactus_gen::stdlib::ui::Grid{.columns = 0, .cell_size = {-10, -20}, .gap = {-3, 2}, .padding = {1, -5}});
    const auto grid_automatic = create_node(registry);
    set_parent(registry, grid_automatic, grid);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(grid_automatic,
                                                            cactus_gen::stdlib::ui::PreferredSize{.min_size = {12, 6}});
    const auto grid_explicit = create_node(registry);
    set_parent(registry, grid_explicit, grid);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(
        grid_explicit, cactus_gen::stdlib::ui::PreferredSize{.min_size = {30, 20}});
    registry.emplace<cactus_gen::stdlib::ui::GridItem>(
        grid_explicit, cactus_gen::stdlib::ui::GridItem{.column = 1, .row = 2, .column_span = 0, .row_span = 2});

    const auto precedence = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Stack>(
        precedence,
        cactus_gen::stdlib::ui::Stack{.axis    = cactus_gen::stdlib::ui::Axis::Horizontal,
                                      .gap     = 1.0F,
                                      .align   = cactus_gen::stdlib::ui::Align::Start,
                                      .padding = {0, 0}});
    registry.emplace<cactus_gen::stdlib::ui::Grid>(
        precedence,
        cactus_gen::stdlib::ui::Grid{.columns = 9, .cell_size = {99, 99}, .gap = {99, 99}, .padding = {99, 99}});
    const auto precedence_child = create_node(registry);
    set_parent(registry, precedence_child, precedence);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(precedence_child,
                                                            cactus_gen::stdlib::ui::PreferredSize{.min_size = {3, 4}});

    const auto anchor_root                                = create_node(registry);
    registry.get<cactus_gen::stdlib::ui::Node>(anchor_root).clip_children = true;
    registry.emplace<cactus_gen::stdlib::ui::Visual>(
        anchor_root, cactus_gen::stdlib::ui::Visual{.scale = {0.5F, 2.0F}, .opacity = 0.8F});
    const auto centered = create_node(registry);
    set_parent(registry, centered, anchor_root);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(
        centered, cactus_gen::stdlib::ui::PreferredSize{.min_size = {100, 50}});
    registry.emplace<cactus_gen::stdlib::ui::Anchors>(centered,
                                                      cactus_gen::stdlib::ui::Anchors{.min        = {0.5F, 0.5F},
                                                                                      .max        = {0.5F, 0.5F},
                                                                                      .pivot      = {0.5F, 0.5F},
                                                                                      .offset     = {10, -5},
                                                                                      .margin_min = {0, 0},
                                                                                      .margin_max = {0, 0}});
    const auto stretched = create_node(registry);
    set_parent(registry, stretched, anchor_root);
    registry.emplace<cactus_gen::stdlib::ui::Anchors>(stretched,
                                                      cactus_gen::stdlib::ui::Anchors{.min        = {0, 0},
                                                                                      .max        = {1, 1},
                                                                                      .pivot      = {0.9F, 0.1F},
                                                                                      .offset     = {0, 0},
                                                                                      .margin_min = {20, 30},
                                                                                      .margin_max = {20, 30}});

    const auto clipping_child = create_node(registry);
    set_parent(registry, clipping_child, anchor_root);
    registry.get<cactus_gen::stdlib::ui::Node>(clipping_child).clip_children = true;
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(
        clipping_child, cactus_gen::stdlib::ui::PreferredSize{.min_size = {100, 100}});
    registry.emplace<cactus_gen::stdlib::ui::Anchors>(clipping_child,
                                                      cactus_gen::stdlib::ui::Anchors{.min        = {0, 0},
                                                                                      .max        = {0, 0},
                                                                                      .pivot      = {0, 0},
                                                                                      .offset     = {600, 300},
                                                                                      .margin_min = {0, 0},
                                                                                      .margin_max = {0, 0}});
    const auto clipping_grandchild = create_node(registry);
    set_parent(registry, clipping_grandchild, clipping_child);
    registry.emplace<cactus_gen::stdlib::ui::Anchors>(clipping_grandchild,
                                                      cactus_gen::stdlib::ui::Anchors{.min        = {0, 0},
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
    registry.get<cactus_gen::stdlib::ui::Node>(atomic_a).z_index = 0;
    const auto atomic_a_high                     = create_node(registry);
    set_parent(registry, atomic_a_high, atomic_a);
    registry.get<cactus_gen::stdlib::ui::Node>(atomic_a_high).z_index = 1000;
    const auto atomic_b                               = create_node(registry);
    set_parent(registry, atomic_b, atomic_root);
    registry.get<cactus_gen::stdlib::ui::Node>(atomic_b).z_index = 1;

    // Sibling order must follow z_index, not creation order: reordered_first is
    // created first but carries the higher z_index, so it must paint after (and
    // therefore receive a larger draw_order than) reordered_second.
    const auto reorder_root  = create_node(registry);
    const auto reorder_first = create_node(registry);
    set_parent(registry, reorder_first, reorder_root);
    registry.get<cactus_gen::stdlib::ui::Node>(reorder_first).z_index = 5;
    const auto reorder_second                         = create_node(registry);
    set_parent(registry, reorder_second, reorder_root);
    registry.get<cactus_gen::stdlib::ui::Node>(reorder_second).z_index = 0;
    const auto reorder_tie_a                           = create_node(registry);
    set_parent(registry, reorder_tie_a, reorder_root);
    const auto reorder_tie_b = create_node(registry);
    set_parent(registry, reorder_tie_b, reorder_root);

    const cactus_gen::stdlib::core::InputPhase input_measure_phase{};
    cactus_gen::detail::standard_ui_layout_runtime::measure_ui_on(registry, input_measure_phase);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(grid).size.x == 62.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(grid).size.y == 42.0F);

    const cactus_gen::stdlib::core::LateTickPhase measure_phase{};
    cactus_gen::detail::standard_ui_layout_runtime::measure_ui_on(registry, measure_phase);
    cactus_gen::detail::standard_ui_layout_runtime::arrange_ui_on(registry, input_measure_phase);

    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(text).size.x == 15.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(text).size.y == 10.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(image).size.x == 32.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(image).size.y == 32.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(root).size.x == 36.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(root).size.y == 53.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(empty).size.x == 2.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(empty).size.y == 4.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(deep).size.x == 7.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(deep).size.y == 9.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(horizontal).size.x == 12.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(horizontal).size.y == 11.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(button).size.x == 40.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(button).size.y == 20.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(overlay).size.x == 8.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(overlay).size.y == 15.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(implicit_overlay).size.x == 13.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(implicit_overlay).size.y == 6.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(grid).size.x == 62.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(grid).size.y == 42.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(precedence).size.x == 3.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::DesiredSize>(precedence).size.y == 4.0F);

    const auto& root_layout   = registry.get<cactus_gen::stdlib::ui::ComputedLayout>(root);
    const auto& text_layout   = registry.get<cactus_gen::stdlib::ui::ComputedLayout>(text);
    const auto& hidden_layout = registry.get<cactus_gen::stdlib::ui::ComputedLayout>(hidden);
    const auto& deep_layout   = registry.get<cactus_gen::stdlib::ui::ComputedLayout>(deep);
    CHECK(root_layout.size.x == 640.0F);
    CHECK(root_layout.size.y == 360.0F);
    CHECK(text_layout.effective_opacity == 0.2F);
    CHECK_FALSE(hidden_layout.effective_visible);
    CHECK_FALSE(deep_layout.effective_visible);
    CHECK(root_layout.draw_order < text_layout.draw_order);
    CHECK(text_layout.draw_order < hidden_layout.draw_order);
    CHECK(hidden_layout.draw_order < deep_layout.draw_order);

    const auto& centered_layout = registry.get<cactus_gen::stdlib::ui::ComputedLayout>(centered);
    CHECK(centered_layout.position.x == 280.0F);
    CHECK(centered_layout.position.y == 150.0F);
    CHECK(centered_layout.size.x == 100.0F);
    CHECK(centered_layout.size.y == 50.0F);
    const auto& stretched_layout = registry.get<cactus_gen::stdlib::ui::ComputedLayout>(stretched);
    CHECK(stretched_layout.position.x == 20.0F);
    CHECK(stretched_layout.position.y == 30.0F);
    CHECK(stretched_layout.size.x == 600.0F);
    CHECK(stretched_layout.size.y == 300.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(anchor_root).size.x == 640.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(anchor_root).size.y == 360.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(centered).effective_opacity == 0.8F);

    const auto& nested_clip = registry.get<cactus_gen::stdlib::ui::ComputedLayout>(clipping_grandchild);
    CHECK(nested_clip.clip_min.x == 600.0F);
    CHECK(nested_clip.clip_min.y == 300.0F);
    CHECK(nested_clip.clip_max.x == 640.0F);
    CHECK(nested_clip.clip_max.y == 360.0F);

    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(structural_root).size.x == 640.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(stale_root).size.y == 360.0F);

    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(atomic_a).draw_order <
          registry.get<cactus_gen::stdlib::ui::ComputedLayout>(atomic_a_high).draw_order);
    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(atomic_a_high).draw_order <
          registry.get<cactus_gen::stdlib::ui::ComputedLayout>(atomic_b).draw_order);

    // reorder_first was created before reorder_second but has the higher z_index,
    // so sibling stacking order must follow z_index rather than creation order.
    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(reorder_second).draw_order <
          registry.get<cactus_gen::stdlib::ui::ComputedLayout>(reorder_first).draw_order);
    // Equal z_index siblings fall back to stable creation ordinal.
    CHECK(registry.get<cactus_gen::stdlib::ui::ComputedLayout>(reorder_tie_a).draw_order <
          registry.get<cactus_gen::stdlib::ui::ComputedLayout>(reorder_tie_b).draw_order);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)