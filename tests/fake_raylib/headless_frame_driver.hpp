#pragma once

// Shared frame-driving helpers for headless behavioral tests. Must be
// included after CACTUS_HEADLESS_GENERATED_CPP (see
// tests/test_blue_square_headless_behavior.cpp for the required include
// order) so generated_init_project/generated_load_project/
// generated_inject_external_event/generated_drain_external_events and
// std_core__frameEvent/std_core__loadEvent — emitted identically into every
// curated example's generated .cpp by the std.core module — are visible.

namespace cactus_headless_test {

// generated_load_project only runs backend-level setup; `on load` rule
// handlers (e.g. a load-time spawn) fire via a separate loadEvent dispatch
// through the activation boundary, mirroring the generated main()'s
// pre-loop block. Skipping this (as an earlier version of this helper did)
// silently no-ops every `on load` handler — invisible for blue-square
// (no `on load` rules) but not for examples that spawn or configure state
// at load time.
inline void dispatch_load_event(entt::registry& registry) {
    auto& boundary_activation  = cactus::runtime::entt_backend::generated_scheduler_state().activation;
    boundary_activation.active = true;
    cactus::runtime::entt_backend::generated_dispatch_event(registry, std_core__loadEvent{});
    cactus::runtime::entt_backend::generated_drain_event_cascade(registry);
    cactus::runtime::entt_backend::generated_commit_activation(registry);
    boundary_activation.active = false;
}

// Initializes and loads the project, then drives exactly one frame with the
// given `dt`. For multi-frame scenarios use drive_frames instead — calling
// this repeatedly on the same registry would re-run init/load each time.
inline void drive_one_frame(entt::registry& registry, float dt) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    dispatch_load_event(registry);
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = dt});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

// Initializes and loads the project once, then drives `frame_count` frames
// in sequence, each with the given fixed `dt`. The fake's call log
// accumulates every frame's calls in order — inspect it with
// cactus_raylib_fake::find_call/ordered_subsequence per frame, or reset()
// between calls to drive_one_frame if only the latest frame matters.
inline void drive_frames(entt::registry& registry, float dt, int frame_count) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    dispatch_load_event(registry);
    for (int frame = 0; frame < frame_count; ++frame) {
        cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = dt});
        cactus::runtime::entt_backend::generated_drain_external_events(registry);
    }
}

// Drives a single frame on an already-initialized/loaded registry (unlike
// drive_one_frame/drive_frames, does not re-run init/load) — for tests that
// need to interleave other events or state changes between frames.
inline void drive_frame(entt::registry& registry, float dt = 1.0F / 60.0F) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = dt});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

}  // namespace cactus_headless_test
