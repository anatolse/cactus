// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using CreationOrdinal = cactus::runtime::entt_backend::CactusCreationOrdinal;

entt::entity make_window_target(entt::registry& registry,
                                const Vector2 position,
                                const Vector2 size,
                                const int draw_order,
                                const bool enabled       = true,
                                const bool blocks_lower  = true,
                                const bool visible       = true) {
    const auto entity = registry.create();
    registry.emplace<CreationOrdinal>(
        entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
    registry.emplace<std_ui__ComputedLayout>(entity,
                                             std_ui__ComputedLayout{.position          = position,
                                                                    .size              = size,
                                                                    .effective_visible = visible,
                                                                    .effective_enabled = true,
                                                                    .effective_opacity = 1.0F,
                                                                    .clip_min          = {.x = 0.0F, .y = 0.0F},
                                                                    .clip_max          = {.x = 800.0F, .y = 600.0F},
                                                                    .draw_order        = draw_order});
    registry.emplace<std_pointer__PointerTarget>(
        entity, std_pointer__PointerTarget{.enabled = enabled, .blocks_lower = blocks_lower, .priority = 0});
    return entity;
}

entt::entity make_flat_box_target(entt::registry& registry, const Vector2 position, const Vector2 size) {
    const auto entity = registry.create();
    registry.emplace<CreationOrdinal>(
        entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
    registry.emplace<std_transform_flat__WorldTransform>(entity, std_transform_flat__WorldTransform{.position = position});
    registry.emplace<std_physics_flat__Collider>(entity);
    registry.emplace<std_physics_flat__BoxCollider>(entity, std_physics_flat__BoxCollider{.size = size});
    registry.emplace<std_pointer__PointerTarget>(entity,
                                                 std_pointer__PointerTarget{.enabled = true, .blocks_lower = true});
    return entity;
}

}  // namespace

TEST_CASE("std.pointer.top_target selects window candidates over world candidates", "[runtime][stdlib][pointer]") {
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto world_entity  = make_flat_box_target(registry, {.x = 50.0F, .y = 50.0F}, {.x = 20.0F, .y = 20.0F});
    const auto window_entity = make_window_target(registry, {.x = 40.0F, .y = 40.0F}, {.x = 20.0F, .y = 20.0F}, 0);

    cactus_raylib_fake::set_mouse_position(Vector2{.x = 50.0F, .y = 50.0F});

    const auto top = cactus::runtime::entt_backend::pointer_top_target(registry);
    CHECK(top == window_entity);
    CHECK(top != world_entity);
}

TEST_CASE("std.pointer.top_target follows reverse painter order among overlapping window siblings",
          "[runtime][stdlib][pointer]") {
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto behind = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 100.0F, .y = 100.0F}, 0);
    const auto front   = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 100.0F, .y = 100.0F}, 5);
    (void)behind;

    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});

    CHECK(cactus::runtime::entt_backend::pointer_top_target(registry) == front);
}

TEST_CASE("std.pointer.top_target respects disabled-blocking and nonblocking pass-through",
          "[runtime][stdlib][pointer]") {
    cactus_raylib_fake::reset();

    SECTION("disabled blocking target prevents click-through to the target behind it") {
        entt::registry registry;
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);

        make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 100.0F, .y = 100.0F}, 0);
        make_window_target(registry,
                           {.x = 0.0F, .y = 0.0F},
                           {.x = 100.0F, .y = 100.0F},
                           5,
                           /*enabled=*/false,
                           /*blocks_lower=*/true);

        cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
        CHECK(cactus::runtime::entt_backend::pointer_top_target(registry) == entt::entity{entt::null});
    }

    SECTION("nonblocking disabled target permits the enabled target behind it") {
        entt::registry registry;
        cactus::runtime::entt_backend::generated_init_project(registry);
        cactus::runtime::entt_backend::generated_load_project(registry);

        const auto lower = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 100.0F, .y = 100.0F}, 0);
        make_window_target(registry,
                           {.x = 0.0F, .y = 0.0F},
                           {.x = 100.0F, .y = 100.0F},
                           5,
                           /*enabled=*/false,
                           /*blocks_lower=*/false);

        cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
        CHECK(cactus::runtime::entt_backend::pointer_top_target(registry) == lower);
    }
}

TEST_CASE("std.pointer.top_target ignores invisible and clipped-out window candidates",
          "[runtime][stdlib][pointer]") {
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    make_window_target(
        registry, {.x = 0.0F, .y = 0.0F}, {.x = 100.0F, .y = 100.0F}, 0, true, true, /*visible=*/false);

    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
    CHECK(cactus::runtime::entt_backend::pointer_top_target(registry) == entt::entity{entt::null});
}

TEST_CASE("std.pointer.top_target hits a flat-world box collider at the converted world position",
          "[runtime][stdlib][pointer]") {
    cactus_raylib_fake::reset();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto inside_box = make_flat_box_target(registry, {.x = 200.0F, .y = 200.0F}, {.x = 40.0F, .y = 40.0F});

    // Default 2D camera (offset=0, target=0, zoom=1) maps screen to world 1:1.
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 210.0F, .y = 205.0F});
    CHECK(cactus::runtime::entt_backend::pointer_top_target(registry) == inside_box);

    cactus_raylib_fake::set_mouse_position(Vector2{.x = 500.0F, .y = 500.0F});
    CHECK(cactus::runtime::entt_backend::pointer_top_target(registry) == entt::entity{entt::null});
}

TEST_CASE("compute_pointer_frame_transitions reports Leave-before-Enter when the hovered target changes",
          "[runtime][stdlib][pointer][router]") {
    cactus_raylib_fake::reset();
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto first  = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);
    const auto second = make_window_target(registry, {.x = 100.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);

    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
    const auto onto_first = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK(onto_first.hover_changed);
    CHECK(onto_first.leave_target == entt::entity{entt::null});
    CHECK(onto_first.enter_target == first);

    // Same position, same frame's target: no repeated transition.
    const auto steady = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK_FALSE(steady.hover_changed);

    cactus_raylib_fake::set_mouse_position(Vector2{.x = 110.0F, .y = 10.0F});
    const auto onto_second = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK(onto_second.hover_changed);
    CHECK(onto_second.leave_target == first);
    CHECK(onto_second.enter_target == second);
}

TEST_CASE("compute_pointer_frame_transitions clears a hovered entity safely once it is destroyed",
          "[runtime][stdlib][pointer][router]") {
    cactus_raylib_fake::reset();
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto target = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
    CHECK(cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry).enter_target == target);

    registry.destroy(target);
    // Moving off any target too, so top_target() is null this frame — the
    // stale hover handle must still be cleared without a Leave delivered to
    // the now-dead entity (nothing left to compare to, so hover_changed
    // reports false: null-to-null is not a transition).
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 900.0F, .y = 900.0F});
    const auto after_destroy = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK_FALSE(after_destroy.hover_changed);
    CHECK(after_destroy.top == entt::entity{entt::null});
}

TEST_CASE("compute_pointer_frame_transitions captures on press and validates Click on release over the same target",
          "[runtime][stdlib][pointer][router]") {
    cactus_raylib_fake::reset();
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto target = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});

    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
    const auto press = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK(press.press_occurred);
    CHECK(press.press_target == target);
    CHECK(press.should_consume_primary);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);

    // Held for a frame with no edge: still an active capture, still consumed.
    const auto held = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK_FALSE(held.press_occurred);
    CHECK(held.should_consume_primary);

    cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
    const auto release = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK(release.release_occurred);
    CHECK(release.release_target == target);
    CHECK(release.release_is_click);
    CHECK(release.should_consume_primary);
}

TEST_CASE("compute_pointer_frame_transitions cancels Click when released outside the captured target",
          "[runtime][stdlib][pointer][router]") {
    cactus_raylib_fake::reset();
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto target = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
    CHECK(cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry).press_target == target);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);

    // Pointer moves off the target before release.
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 900.0F, .y = 900.0F});
    cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
    const auto release = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK(release.release_occurred);
    CHECK(release.release_target == target);
    CHECK_FALSE(release.release_is_click);
}

TEST_CASE("compute_pointer_frame_transitions clears a captured entity safely once it is destroyed before release",
          "[runtime][stdlib][pointer][router]") {
    cactus_raylib_fake::reset();
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto target = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
    CHECK(cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry).press_target == target);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);

    registry.destroy(target);
    cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
    const auto release = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK_FALSE(release.release_occurred);
    CHECK_FALSE(release.release_is_click);
}

TEST_CASE("compute_pointer_frame_transitions leaves the primary action unconsumed on a miss",
          "[runtime][stdlib][pointer][router]") {
    cactus_raylib_fake::reset();
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    // No targets anywhere near the pointer.
    make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 900.0F, .y = 900.0F});
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);

    const auto miss = cactus::runtime::entt_backend::compute_pointer_frame_transitions(registry);
    CHECK_FALSE(miss.press_occurred);
    CHECK_FALSE(miss.should_consume_primary);
}

TEST_CASE("RoutePointer end-to-end: hover, capture, release-inside Click, and PointerState/log delivery",
          "[runtime][stdlib][pointer][router][e2e]") {
    cactus_raylib_fake::reset();
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus_headless_test::dispatch_load_event(registry);

    const auto target = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);
    registry.emplace<std_pointer__PointerState>(target);
    registry.emplace<pointer_candidates_runtime__PointerEventLog>(target);

    // Frame 1: pointer enters the target, no button activity yet.
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
    cactus_headless_test::drive_frame(registry);
    {
        const auto& state = registry.get<std_pointer__PointerState>(target);
        const auto& log   = registry.get<pointer_candidates_runtime__PointerEventLog>(target);
        CHECK(state.hovered);
        CHECK(log.enter_count == 1);
        CHECK(log.leave_count == 0);
    }

    // Frame 2: press.
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
    cactus_headless_test::drive_frame(registry);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);
    {
        const auto& state = registry.get<std_pointer__PointerState>(target);
        const auto& log   = registry.get<pointer_candidates_runtime__PointerEventLog>(target);
        CHECK(state.pressed);
        CHECK(log.press_count == 1);
    }

    // Frame 3: release while still over the target -> Click.
    cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
    cactus_headless_test::drive_frame(registry);
    {
        const auto& state = registry.get<std_pointer__PointerState>(target);
        const auto& log   = registry.get<pointer_candidates_runtime__PointerEventLog>(target);
        CHECK_FALSE(state.pressed);
        CHECK(log.release_count == 1);
        CHECK(log.click_count == 1);
    }

    // Frame 4: pointer leaves the target entirely.
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 900.0F, .y = 900.0F});
    cactus_headless_test::drive_frame(registry);
    {
        const auto& state = registry.get<std_pointer__PointerState>(target);
        const auto& log   = registry.get<pointer_candidates_runtime__PointerEventLog>(target);
        CHECK_FALSE(state.hovered);
        CHECK(log.leave_count == 1);
    }
}

TEST_CASE("RoutePointer end-to-end: release outside the target delivers PointerRelease but not Click",
          "[runtime][stdlib][pointer][router][e2e]") {
    cactus_raylib_fake::reset();
    cactus::runtime::entt_backend::reset_pointer_router_state();

    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus_headless_test::dispatch_load_event(registry);

    const auto target = make_window_target(registry, {.x = 0.0F, .y = 0.0F}, {.x = 50.0F, .y = 50.0F}, 0);
    registry.emplace<std_pointer__PointerState>(target);
    registry.emplace<pointer_candidates_runtime__PointerEventLog>(target);

    cactus_raylib_fake::set_mouse_position(Vector2{.x = 10.0F, .y = 10.0F});
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, true);
    cactus_headless_test::drive_frame(registry);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(0, false);

    cactus_raylib_fake::set_mouse_position(Vector2{.x = 900.0F, .y = 900.0F});
    cactus_raylib_fake::set_mouse_button_released_this_frame(0, true);
    cactus_headless_test::drive_frame(registry);

    const auto& log = registry.get<pointer_candidates_runtime__PointerEventLog>(target);
    CHECK(log.release_count == 1);
    CHECK(log.click_count == 0);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
