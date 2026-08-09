// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"

#include <catch2/catch_test_macros.hpp>

// End-to-end coverage for examples/standard-ui/main.cactus (openspec
// add-standard-ui, task 11.2): drives the example's own authored entity
// tree — not synthetic fixtures — through real MeasureUi/ArrangeUi/
// RoutePointer frames, exercising layout values, painter/top-hit
// agreement, capture, Click delivery, bump restart, and gameplay event
// emission together, the way a player's mouse actually would. Per-formula
// coverage for Stack/Grid/Anchors/pointer routing already lives in
// test_standard_ui_layout_headless_behavior.cpp and
// test_pointer_candidates_headless_behavior.cpp; this file only checks
// that the real example wires all of it together correctly.
//
// ComputedLayout/DesiredSize are frame-local *projected* traits: they are
// only visible for the remainder of the std.core.input/late_tick/render
// pipeline that projected them — generated_process_root_event calls
// clear_projected_traits() at the very end of every frame, which rolls each
// projected trait back to whatever it held before that frame (nothing, for
// a fresh entity), removing the component again. So layout values can never
// be read from the registry *after* drive_frame() returns; they must be
// read immediately after calling MeasureUi/ArrangeUi directly, exactly as
// test_standard_ui_layout_headless_behavior.cpp does. This test therefore
// snapshots layout once via a direct measure/arrange call up front, and
// drives every subsequent frame using that snapshot's coordinates rather
// than re-reading ComputedLayout from the registry.

namespace {

constexpr int kWindowWidth  = 960;
constexpr int kWindowHeight = 540;
constexpr float kDt         = 1.0F / 60.0F;

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = kDt});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

// ClickCounter is only ever attached to the one entity in this scene
// (ClickButton), so it doubles as a stable handle onto it — the example
// spawns its tree at module load and returns no other reference to it.
entt::entity find_click_button(entt::registry& registry) {
    const auto view = registry.view<main__ClickCounter>();
    REQUIRE(view.begin() != view.end());
    return *view.begin();
}

entt::entity find_center_panel(entt::registry& registry) {
    const auto view = registry.view<std_ui__PreferredSize>();
    for (const auto entity : view) {
        const auto& preferred = view.get<std_ui__PreferredSize>(entity);
        if (preferred.min_size.x == 360.0F && preferred.min_size.y == 240.0F) {
            return entity;
        }
    }
    FAIL("CenterPanel (PreferredSize 360x240) not found");
    return entt::entity{entt::null};
}

struct Layout {
    Vector2 position;
    Vector2 size;
};

struct SceneLayout {
    Layout center_panel;
    Layout click_button;
};

// Runs MeasureUi/ArrangeUi's input-phase handlers directly (see file
// comment) and snapshots the two entities this test cares about before
// clear_projected_traits would otherwise erase ComputedLayout again.
SceneLayout measure_scene(entt::registry& registry) {
    const cactus::runtime::entt_backend::std_core__inputPhaseRuntimeState phase{};
    main__measure_ui_std_core__input(registry, phase);
    main__arrange_ui_std_core__input(registry, phase);

    const auto center_panel   = find_center_panel(registry);
    const auto click_button   = find_click_button(registry);
    const auto& panel_layout  = registry.get<std_ui__ComputedLayout>(center_panel);
    const auto& button_layout = registry.get<std_ui__ComputedLayout>(click_button);
    return SceneLayout{.center_panel = {.position = panel_layout.position, .size = panel_layout.size},
                       .click_button = {.position = button_layout.position, .size = button_layout.size}};
}

}  // namespace

TEST_CASE("standard-ui example: layout, top-hit agreement, capture, Click delivery, and bump restart",
          "[example-behavior][standard-ui][e2e]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);
    cactus_raylib_fake::set_screen_size(kWindowWidth, kWindowHeight);
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    // ── Layout values ───────────────────────────────────────────────────
    // CenterPanel is anchored center/center on a 960x540 root with a
    // 360x240 PreferredSize floor that its own (smaller) measured content
    // never exceeds, so it lands at the window's exact center. ClickButton
    // is the last child in CenterPanel's vertical, center-aligned Stack;
    // its measured size (button_intrinsic("Click Me", padding)) and
    // stacked position are fully determined by the fake's deterministic
    // text_size formula (length * font_size * 0.5 wide, font_size tall).
    const auto scene = measure_scene(registry);
    CHECK(scene.center_panel.position.x == 300.0F);
    CHECK(scene.center_panel.position.y == 150.0F);
    CHECK(scene.center_panel.size.x == 360.0F);
    CHECK(scene.center_panel.size.y == 240.0F);
    CHECK(scene.click_button.position.x == 436.0F);
    CHECK(scene.click_button.position.y == 342.0F);
    CHECK(scene.click_button.size.x == 88.0F);
    CHECK(scene.click_button.size.y == 32.0F);

    const Vector2 button_center{.x = scene.click_button.position.x + (scene.click_button.size.x / 2.0F),
                                .y = scene.click_button.position.y + (scene.click_button.size.y / 2.0F)};
    const auto click_button = find_click_button(registry);

    // ── Painter/top-hit agreement ───────────────────────────────────────
    // pointer_top_target() reads ComputedLayout directly, so — like the
    // layout snapshot above — it must be checked right after a fresh
    // measure/arrange pass, not after a full drive_frame() (whose
    // end-of-frame clear_projected_traits would already have erased it).
    // The point under the button center resolves to the button itself —
    // the same generic hit-test std.ui's painter order feeds — because the
    // button paints on top of everything behind it at that point.
    cactus_raylib_fake::set_mouse_position(button_center);
    measure_scene(registry);
    CHECK(cactus::runtime::entt_backend::pointer_top_target(registry) == click_button);

    // ── Capture, Click delivery, gameplay event emission ────────────────
    REQUIRE(registry.get<main__ClickCounter>(click_button).count == 0);
    REQUIRE_FALSE(registry.get<std_ui__BumpAnimation>(click_button).playing);

    // Press: primary capture engages on the button.
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
    drive_frame(registry);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);
    CHECK(registry.get<std_pointer__PointerState>(click_button).pressed);

    // Release over the same target: Click is delivered to the real
    // ClickButtonGameplay rule (an ordinary `filter: ClickCounter` handler
    // reading a targeted pointer.Click through `self`), which increments
    // the gameplay counter and starts the bump — this is the exact
    // self-reference + targeted-emit path that had to be fixed in
    // semantic_analyzer.cpp for a dotted cross-module event trigger
    // (`on pointer.Click:`) to work at all.
    cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
    drive_frame(registry);
    CHECK_FALSE(registry.get<std_pointer__PointerState>(click_button).pressed);
    CHECK(registry.get<main__ClickCounter>(click_button).count == 1);

    const auto& bump_after_click = registry.get<std_ui__BumpAnimation>(click_button);
    CHECK(bump_after_click.playing);
    CHECK(bump_after_click.elapsed < bump_after_click.duration);

    // ── Bump restart ─────────────────────────────────────────────────────
    // Let the bump advance for a few idle frames (still short of its
    // 0.15s duration), then click again before it finishes: a fresh
    // StartBump must restart elapsed rather than let it keep accumulating.
    for (int i = 0; i < 3; ++i) {
        drive_frame(registry);
    }
    const float elapsed_before_restart = registry.get<std_ui__BumpAnimation>(click_button).elapsed;
    REQUIRE(elapsed_before_restart < registry.get<std_ui__BumpAnimation>(click_button).duration);

    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
    drive_frame(registry);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);
    cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
    drive_frame(registry);

    const auto& bump_after_restart = registry.get<std_ui__BumpAnimation>(click_button);
    CHECK(bump_after_restart.playing);
    CHECK(bump_after_restart.elapsed < elapsed_before_restart);
    CHECK(registry.get<main__ClickCounter>(click_button).count == 2);
}

TEST_CASE("standard-ui example: capture persists off-target, so releasing outside ClickButton cancels the Click",
          "[example-behavior][standard-ui][e2e]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);
    cactus_raylib_fake::set_screen_size(kWindowWidth, kWindowHeight);
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto scene        = measure_scene(registry);
    const auto click_button = find_click_button(registry);
    const Vector2 button_center{.x = scene.click_button.position.x + (scene.click_button.size.x / 2.0F),
                                .y = scene.click_button.position.y + (scene.click_button.size.y / 2.0F)};

    cactus_raylib_fake::set_mouse_position(button_center);
    drive_frame(registry);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
    drive_frame(registry);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);
    REQUIRE(registry.get<std_pointer__PointerState>(click_button).pressed);

    // Move off the button entirely before releasing: still a captured
    // release (PointerState clears), but not a valid Click.
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 5.0F, .y = 5.0F});
    cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
    drive_frame(registry);

    CHECK_FALSE(registry.get<std_pointer__PointerState>(click_button).pressed);
    CHECK(registry.get<main__ClickCounter>(click_button).count == 0);
    CHECK_FALSE(registry.get<std_ui__BumpAnimation>(click_button).playing);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
