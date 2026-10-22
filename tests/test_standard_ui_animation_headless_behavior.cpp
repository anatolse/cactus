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

using CreationOrdinal = cactus::runtime::entt_backend::CreationOrdinal;

entt::entity create_node(entt::registry& registry) {
    const auto entity = registry.create();
    registry.emplace<CreationOrdinal>(
        entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
    // Every field explicit, matching the DSL's declared defaults: a bare
    // emplace<T>() zero-initializes (visible = false), unlike an authored
    // `entity X: ui.Node` declaration, which the compiler always lowers to
    // an explicit assignment per field regardless of whether the author
    // overrode it.
    registry.emplace<cactus_gen::stdlib::ui::Node>(
        entity, cactus_gen::stdlib::ui::Node{.visible = true, .enabled = true, .z_index = 0, .clip_children = false});
    return entity;
}

// Mirrors cactus_headless_test::dispatch_load_event's activation-boundary
// shape, but for a targeted custom event rather than the untargeted load
// event — std.ui.StartBump has no DSL emit site to lower for us here, so the
// test drives the same runtime primitives generated `emit ... to ...` code
// would use.
void emit_targeted(entt::registry& registry, entt::entity target) {
    auto& activation  = cactus::runtime::entt_backend::generated_scheduler_state().activation;
    activation.active = true;
    cactus::runtime::entt_backend::generated_emit_targeted_event(cactus_gen::stdlib::ui::StartBumpEvent{}, target);
    cactus::runtime::entt_backend::generated_drain_event_cascade(registry);
    cactus::runtime::entt_backend::generated_commit_activation(registry);
    activation.active = false;
}

}  // namespace

TEST_CASE("AnimateUiFrames advances frame deterministically and leaves paused/invalid animations unchanged",
          "[runtime][stdlib][ui][animation]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto playing = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::FrameAnimation>(
        playing,
        cactus_gen::stdlib::ui::FrameAnimation{
            .frame_count = 4, .fps = 10.0F, .frame = 0, .elapsed = 0.0F, .playing = true});

    const auto paused = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::FrameAnimation>(
        paused,
        cactus_gen::stdlib::ui::FrameAnimation{
            .frame_count = 4, .fps = 10.0F, .frame = 2, .elapsed = 0.0F, .playing = false});

    const auto zero_fps = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::FrameAnimation>(
        zero_fps,
        cactus_gen::stdlib::ui::FrameAnimation{
            .frame_count = 4, .fps = 0.0F, .frame = 1, .elapsed = 0.0F, .playing = true});

    const auto invalid_count = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::FrameAnimation>(
        invalid_count,
        cactus_gen::stdlib::ui::FrameAnimation{
            .frame_count = 0, .fps = 10.0F, .frame = 0, .elapsed = 0.0F, .playing = true});

    const cactus_gen::stdlib::core::TickPhase tick_phase{.dt = 1.0F / 60.0F};
    // 10 fps: one whole frame every 0.1s. 0.25s of ticks -> floor(0.25 * 10) = 2 frames advanced.
    for (int i = 0; i < 15; ++i) {
        cactus_gen::standard_ui_layout_runtime::animate_ui_frames_std_core::tick(registry, tick_phase);
    }

    CHECK(registry.get<cactus_gen::stdlib::ui::FrameAnimation>(playing).frame == 2);
    CHECK(registry.get<cactus_gen::stdlib::ui::FrameAnimation>(paused).frame == 2);    // unchanged: not playing
    CHECK(registry.get<cactus_gen::stdlib::ui::FrameAnimation>(zero_fps).frame == 1);  // unchanged: fps <= 0
    CHECK(registry.get<cactus_gen::stdlib::ui::FrameAnimation>(invalid_count).frame ==
          0);  // clamps to a 1-frame cycle: always 0
}

TEST_CASE("AnimateUiFrames wraps frame modulo the clamped frame count", "[runtime][stdlib][ui][animation]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto entity = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::FrameAnimation>(
        entity,
        cactus_gen::stdlib::ui::FrameAnimation{
            .frame_count = 3, .fps = 10.0F, .frame = 0, .elapsed = 0.0F, .playing = true});

    const cactus_gen::stdlib::core::TickPhase tick_phase{.dt = 1.0F / 60.0F};
    // 0.7s at 10fps -> 7 whole frames elapsed; 7 % 3 == 1.
    for (int i = 0; i < 42; ++i) {
        cactus_gen::standard_ui_layout_runtime::animate_ui_frames_std_core::tick(registry, tick_phase);
    }

    CHECK(registry.get<cactus_gen::stdlib::ui::FrameAnimation>(entity).frame == 1);
}

TEST_CASE("StartBump restarts the bump on its targeted recipient only", "[runtime][stdlib][ui][animation]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus_headless_test::dispatch_load_event(registry);

    const auto targeted = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Visual>(
        targeted, cactus_gen::stdlib::ui::Visual{.scale = {.x = 1.0F, .y = 1.0F}, .opacity = 1.0F});
    registry.emplace<cactus_gen::stdlib::ui::BumpAnimation>(
        targeted,
        cactus_gen::stdlib::ui::BumpAnimation{.from_scale = {.x = 0.5F, .y = 0.5F},
                                              .to_scale   = {.x = 1.0F, .y = 1.0F},
                                              .duration   = 1.0F,
                                              .elapsed    = 5.0F,
                                              .playing    = false});

    const auto bystander = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Visual>(
        bystander, cactus_gen::stdlib::ui::Visual{.scale = {.x = 1.0F, .y = 1.0F}, .opacity = 1.0F});
    registry.emplace<cactus_gen::stdlib::ui::BumpAnimation>(
        bystander,
        cactus_gen::stdlib::ui::BumpAnimation{.from_scale = {.x = 0.5F, .y = 0.5F},
                                              .to_scale   = {.x = 1.0F, .y = 1.0F},
                                              .duration   = 1.0F,
                                              .elapsed    = 5.0F,
                                              .playing    = false});

    emit_targeted(registry, targeted);

    CHECK(registry.get<cactus_gen::stdlib::ui::BumpAnimation>(targeted).playing);
    CHECK(registry.get<cactus_gen::stdlib::ui::BumpAnimation>(targeted).elapsed == 0.0F);
    // The untargeted bystander's terminal (finished, not playing) state is untouched.
    CHECK_FALSE(registry.get<cactus_gen::stdlib::ui::BumpAnimation>(bystander).playing);
    CHECK(registry.get<cactus_gen::stdlib::ui::BumpAnimation>(bystander).elapsed == 5.0F);
}

TEST_CASE("AnimateUiBump's StartBump and tick handlers are independently triggerable (separate graph nodes)",
          "[runtime][stdlib][ui][animation]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus_headless_test::dispatch_load_event(registry);

    const auto entity = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::Visual>(
        entity, cactus_gen::stdlib::ui::Visual{.scale = {.x = 1.0F, .y = 1.0F}, .opacity = 1.0F});
    registry.emplace<cactus_gen::stdlib::ui::BumpAnimation>(
        entity,
        cactus_gen::stdlib::ui::BumpAnimation{.from_scale = {.x = 0.5F, .y = 0.5F},
                                              .to_scale   = {.x = 1.0F, .y = 1.0F},
                                              .duration   = 1.0F,
                                              .elapsed    = 0.0F,
                                              .playing    = false});

    // The tick handler alone, with playing == false, never resets elapsed or
    // touches scale -- proving it does not implicitly fold in StartBump's
    // reset behavior.
    const cactus_gen::stdlib::core::TickPhase tick_phase{.dt = 1.0F / 60.0F};
    cactus_gen::standard_ui_layout_runtime::animate_ui_bump_std_core::tick(registry, tick_phase);
    CHECK_FALSE(registry.get<cactus_gen::stdlib::ui::BumpAnimation>(entity).playing);
    CHECK(registry.get<cactus_gen::stdlib::ui::Visual>(entity).scale.x == 1.0F);

    // StartBump alone, with no subsequent tick, restarts state but does not
    // itself advance elapsed or interpolate scale -- proving the tick
    // handler's interpolation is not implicitly folded into StartBump.
    emit_targeted(registry, entity);
    CHECK(registry.get<cactus_gen::stdlib::ui::BumpAnimation>(entity).playing);
    CHECK(registry.get<cactus_gen::stdlib::ui::BumpAnimation>(entity).elapsed == 0.0F);
    CHECK(registry.get<cactus_gen::stdlib::ui::Visual>(entity).scale.x == 1.0F);  // still at the pre-bump scale
}

TEST_CASE("A tick-phase bump changes render presentation the same frame without moving ComputedLayout's hit bounds",
          "[runtime][stdlib][ui][animation]") {
    cactus_raylib_fake::reset();
    // A window-root entity's inherited clip defaults to window_size(), which
    // is {0,0} (and so degenerate -- render_ui_primitive would skip drawing
    // entirely) unless the window is "ready".
    cactus_raylib_fake::set_window_ready(true);
    cactus_raylib_fake::set_screen_size(800, 600);

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto entity = create_node(registry);
    registry.emplace<cactus_gen::stdlib::ui::PreferredSize>(
        entity, cactus_gen::stdlib::ui::PreferredSize{.min_size = {.x = 40.0F, .y = 40.0F}});
    // Fixed-size anchors (min == max) so ComputedLayout uses this entity's
    // own 40x40 DesiredSize rather than filling the 800x600 window slot.
    registry.emplace<cactus_gen::stdlib::ui::Anchors>(
        entity,
        cactus_gen::stdlib::ui::Anchors{.min    = {.x = 0.0F, .y = 0.0F},
                                        .max    = {.x = 0.0F, .y = 0.0F},
                                        .pivot  = {.x = 0.0F, .y = 0.0F},
                                        .offset = {.x = 0.0F, .y = 0.0F}});
    registry.emplace<cactus_gen::stdlib::ui::Visual>(
        entity, cactus_gen::stdlib::ui::Visual{.scale = {.x = 1.0F, .y = 1.0F}, .opacity = 1.0F});
    registry.emplace<cactus_gen::stdlib::ui::Panel>(entity, cactus_gen::stdlib::ui::Panel{.background = RED});
    // Already mid-flight so a single driven frame visibly interpolates scale.
    registry.emplace<cactus_gen::stdlib::ui::BumpAnimation>(
        entity,
        cactus_gen::stdlib::ui::BumpAnimation{.from_scale = {.x = 0.0F, .y = 0.0F},
                                              .to_scale   = {.x = 2.0F, .y = 2.0F},
                                              .duration   = 1.0F,
                                              .elapsed    = 0.5F,
                                              .playing    = true});

    // Drives input -> tick -> late_tick -> render directly by phase function,
    // like test_standard_ui_layout_headless_behavior.cpp, rather than through
    // the full drive_frame(): that also runs frame-end projected-trait
    // cleanup, which would remove ComputedLayout before this test could read
    // it back.
    const cactus_gen::stdlib::core::InputPhase input_phase{};
    cactus_gen::standard_ui_layout_runtime::measure_ui_std_core::input(registry, input_phase);
    cactus_gen::standard_ui_layout_runtime::arrange_ui_std_core::input(registry, input_phase);

    const cactus_gen::stdlib::core::TickPhase tick_phase{.dt = 1.0F / 60.0F};
    cactus_gen::standard_ui_layout_runtime::animate_ui_bump_std_core::tick(registry, tick_phase);

    const cactus_gen::stdlib::core::LateTickPhase late_tick_phase{};
    cactus_gen::standard_ui_layout_runtime::measure_ui_std_core::late_tick(registry, late_tick_phase);
    cactus_gen::standard_ui_layout_runtime::arrange_ui_std_core::late_tick(registry, late_tick_phase);

    cactus_gen::standard_ui_layout_runtime::render_ui_tick(registry);
    cactus::runtime::entt_backend::end_render_frame();

    // Logical/hit bounds come from PreferredSize alone; Visual.scale never
    // feeds ArrangeUi (design decision #6 — "Visual scale ... does not
    // change logical layout or pointer hit bounds").
    const auto& layout = registry.get<cactus_gen::stdlib::ui::ComputedLayout>(entity);
    CHECK(layout.size.x == 40.0F);
    CHECK(layout.size.y == 40.0F);

    // But the render pass this same frame reflects the interpolated
    // (larger, mid-bump) presentation scale, not the logical size.
    const auto* draw =
        cactus_raylib_fake::find_call<cactus_raylib_fake::RecordedDrawRectangleRec>(cactus_raylib_fake::call_log());
    REQUIRE(draw != nullptr);
    CHECK(draw->rec.width > 40.0F);
    CHECK(draw->rec.height > 40.0F);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
