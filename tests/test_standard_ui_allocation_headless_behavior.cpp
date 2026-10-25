// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#include "heap_allocation_counter.hpp"

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

namespace {

namespace ui     = cactus_gen::stdlib::ui;
namespace layout = cactus_gen::detail::standard_ui_layout_runtime;
using cactus::runtime::entt_backend::CreationOrdinal;

struct Hud {
    entt::registry registry;
    std::vector<entt::entity> nodes;

    Hud() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        cactus_raylib_fake::set_screen_size(640, 360);
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);

        const auto root = node(entt::null, 0);
        registry.emplace<ui::Stack>(
            root, ui::Stack{.axis = ui::Axis::Vertical, .gap = 2.0F, .align = ui::Align::Center, .padding = {3, 4}});
        for (int i = 0; i < 6; ++i) {
            sized(node(root, (i * 7) % 3), 10.0F + static_cast<float>(i), 5.0F);
        }
        const auto row = node(root, 1);
        registry.emplace<ui::Stack>(
            row, ui::Stack{.axis = ui::Axis::Horizontal, .gap = 1.0F, .align = ui::Align::End, .padding = {1, 1}});
        for (int i = 0; i < 4; ++i) {
            const auto cell = node(row, 2 - i);
            registry.emplace<ui::Text>(
                cell, ui::Text{.value = "ab", .font_size = 8 + i, .color = WHITE, .align = ui::TextAlign::Start});
        }
        const auto grid = node(entt::null, 2);
        registry.emplace<ui::Grid>(grid,
                                   ui::Grid{.columns = 3, .cell_size = {4, 4}, .gap = {1, 2}, .padding = {2, 2}});
        for (int i = 0; i < 7; ++i) {
            sized(node(grid, i % 2), 6.0F, 3.0F + static_cast<float>(i));
        }
        const auto overlay = node(entt::null, -1);
        registry.emplace<ui::Overlay>(overlay, ui::Overlay{.padding = {2, 3}});
        const auto anchored = node(overlay, 0);
        registry.emplace<ui::Anchors>(anchored,
                                      ui::Anchors{.min        = {0.5F, 0.5F},
                                                  .max        = {0.5F, 0.5F},
                                                  .pivot      = {0.5F, 0.5F},
                                                  .offset     = {1, -1},
                                                  .margin_min = {0, 0},
                                                  .margin_max = {0, 0}});
        sized(anchored, 20.0F, 10.0F);
        sized(node(anchored, 3), 2.0F, 2.0F);
        sized(node(overlay, -2), 8.0F, 8.0F);
    }

    entt::entity node(entt::entity parent, int z_index) {
        const auto entity = registry.create();
        registry.emplace<CreationOrdinal>(
            entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
        registry.emplace<ui::Node>(entity).z_index = z_index;
        if (parent != entt::null) {
            registry.emplace<cactus_gen::stdlib::core::Parent>(entity, cactus_gen::stdlib::core::Parent{.parent = parent});
        }
        nodes.push_back(entity);
        return entity;
    }

    void sized(entt::entity entity, float width, float height) {
        registry.emplace<ui::PreferredSize>(entity, ui::PreferredSize{.min_size = {width, height}});
    }

    void arrange() {
        layout::measure_ui_on(registry, cactus_gen::stdlib::core::InputPhase{});
        layout::arrange_ui_on(registry, cactus_gen::stdlib::core::InputPhase{});
        layout::measure_ui_on(registry, cactus_gen::stdlib::core::LateTickPhase{});
        layout::arrange_ui_on(registry, cactus_gen::stdlib::core::LateTickPhase{});
    }

    void frame() {
        arrange();
        cactus_gen::detail::clear_projected_traits(registry);
    }

    struct Arranged {
        ui::ComputedLayout layout;
        ui::DesiredSize desired;
    };

    std::vector<Arranged> arranged() {
        arrange();
        std::vector<Arranged> result;
        for (const auto entity : nodes) {
            result.push_back({.layout = registry.get<ui::ComputedLayout>(entity),
                              .desired = registry.get<ui::DesiredSize>(entity)});
        }
        cactus_gen::detail::clear_projected_traits(registry);
        return result;
    }
};

bool same(const ui::ComputedLayout& left, const ui::ComputedLayout& right) {
    return left.position.x == right.position.x && left.position.y == right.position.y &&
           left.size.x == right.size.x && left.size.y == right.size.y &&
           left.effective_visible == right.effective_visible && left.effective_enabled == right.effective_enabled &&
           left.effective_opacity == right.effective_opacity && left.clip_min.x == right.clip_min.x &&
           left.clip_min.y == right.clip_min.y && left.clip_max.x == right.clip_max.x &&
           left.clip_max.y == right.clip_max.y && left.draw_order == right.draw_order;
}

}  // namespace

TEST_CASE("Standard UI: a warmed layout frame makes no heap allocation", "[runtime][stdlib][ui][layout]") {
    Hud hud;
    hud.frame();
    hud.frame();

    std::size_t allocations = 0;
    {
        const cactus_test::AllocationCount counting;
        hud.frame();
        hud.frame();
        allocations = cactus_test::AllocationCount::count();
    }
    CHECK(allocations == 0);
}

TEST_CASE("Standard UI: layout is the same with child snapshots forced", "[runtime][stdlib][ui][layout]") {
    Hud hud;
    const auto lazy = hud.arranged();
    cactus::runtime::entt_backend::set_hierarchy_snapshots_forced(true);
    const auto snapshotted = hud.arranged();
    cactus::runtime::entt_backend::set_hierarchy_snapshots_forced(false);

    REQUIRE(lazy.size() == snapshotted.size());
    for (std::size_t i = 0; i < lazy.size(); ++i) {
        CHECK(same(lazy[i].layout, snapshotted[i].layout));
        CHECK(lazy[i].desired.size.x == snapshotted[i].desired.size.x);
        CHECK(lazy[i].desired.size.y == snapshotted[i].desired.size.y);
    }
}

TEST_CASE("Standard UI: a second arrangement pass in one frame sees a z_index change",
          "[runtime][stdlib][ui][layout]") {
    Hud hud;
    auto& registry   = hud.registry;
    const auto first = hud.nodes[1];
    const auto last  = hud.nodes[6];
    registry.get<ui::Node>(first).z_index = 0;
    registry.get<ui::Node>(last).z_index  = 0;

    layout::measure_ui_on(registry, cactus_gen::stdlib::core::InputPhase{});
    layout::arrange_ui_on(registry, cactus_gen::stdlib::core::InputPhase{});
    CHECK(registry.get<ui::ComputedLayout>(first).draw_order < registry.get<ui::ComputedLayout>(last).draw_order);

    registry.get<ui::Node>(first).z_index = 9;
    layout::arrange_ui_on(registry, cactus_gen::stdlib::core::InputPhase{});
    CHECK(registry.get<ui::ComputedLayout>(last).draw_order < registry.get<ui::ComputedLayout>(first).draw_order);
    cactus_gen::detail::clear_projected_traits(registry);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
