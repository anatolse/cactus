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

#include <cmath>

namespace {

constexpr float kDt = 1.0F / 60.0F;

bool near_x(const float value, const float target) {
    return std::abs(value - target) < 0.01F;
}

// Restarter's clip changes mid-test (interruption), so owners are
// distinguished by their fixed world position, not their current clip.
// Filtered on PlaybackLog too, so MainCamera (also at a whole-number x) never
// matches.
entt::entity entity_at_x(entt::registry& registry, const float x) {
    const auto view = registry.view<model_animation_events__PlaybackLog, std_transform_volume__WorldTransform>();
    for (auto entity : view) {
        if (near_x(registry.get<std_transform_volume__WorldTransform>(entity).position.x, x)) {
            return entity;
        }
    }
    FAIL("no owner found at x=" << x);
    return entt::null;
}

const model_animation_events__PlaybackLog& log_at_x(entt::registry& registry, const float x) {
    return registry.get<model_animation_events__PlaybackLog>(entity_at_x(registry, x));
}

}  // namespace

TEST_CASE("model-animation-events headless: Once playback crosses its cue then completes exactly once",
          "[example-behavior][dsl-model-animation-events]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;

    // Jumper: clip 3 (Robot_Jump) at speed 2.0 crosses its 0.1s cue well
    // before finishing but hasn't finished yet. drive_frames re-inits/loads
    // the project, so only the first call may use it; later calls in this
    // test use drive_frame to keep advancing the same entities.
    cactus_headless_test::drive_frames(registry, kDt, 5);
    {
        const auto& log = log_at_x(registry, -2.0F);
        CHECK(log.marker_count == 1);
        CHECK(log.last_marker_name == "liftoff");
        CHECK(log.finished_count == 0);
    }

    // Enough additional frames to clear the clip's duration at 2x speed.
    for (int i = 0; i < 40; ++i) {
        cactus_headless_test::drive_frame(registry, kDt);
    }
    {
        const auto& log = log_at_x(registry, -2.0F);
        CHECK(log.marker_count == 1);  // Once never replays its cue.
        CHECK(log.finished_count == 1);
        CHECK(log.finished_clip == 3);
    }

    // No duplicate completion: driving further frames must not re-fire it.
    for (int i = 0; i < 20; ++i) {
        cactus_headless_test::drive_frame(registry, kDt);
    }
    {
        const auto& log = log_at_x(registry, -2.0F);
        CHECK(log.marker_count == 1);
        CHECK(log.finished_count == 1);
    }
}

TEST_CASE("model-animation-events headless: Loop playback re-crosses its cue every traversal and never completes",
          "[example-behavior][dsl-model-animation-events]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;

    // Sitter: clip 7 (Robot_Sitting, ~0.417s) at speed 1.0 loops roughly every
    // 25 frames; 90 frames (1.5s) crosses its 0.1s cue at least twice.
    cactus_headless_test::drive_frames(registry, kDt, 90);

    const auto& log = log_at_x(registry, 2.0F);
    CHECK(log.marker_count >= 2);
    CHECK(log.last_marker_name == "loop-mark");
    CHECK(log.finished_count == 0);
}

TEST_CASE("model-animation-events headless: two owners sharing one model asset stay independent",
          "[example-behavior][dsl-model-animation-events]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;
    cactus_headless_test::drive_frames(registry, kDt, 90);

    const auto& jumper_log = log_at_x(registry, -2.0F);
    const auto& sitter_log = log_at_x(registry, 2.0F);

    CHECK(jumper_log.last_marker_name != "loop-mark");
    CHECK(sitter_log.last_marker_name != "liftoff");
    CHECK(jumper_log.finished_count == 1);
    CHECK(sitter_log.finished_count == 0);
}

TEST_CASE(
    "model-animation-events headless: explicit seek skips replay, clip interruption clears completion, "
    "and revision restart rearms it",
    "[example-behavior][dsl-model-animation-events]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;

    // DriveRestartScenario (examples/model_animation_events.cactus) drives
    // Restarter through: normal Once playback on clip 3 (frames 1-29), an
    // explicit seek to time=0.3 at frame 30 (must not replay the 0.05s cue
    // already crossed around frame 3), an interruption to clip 7 at time=0.0
    // at frame 40 (clip 3 never reached its ~0.708s duration, so no
    // completion is synthesized for it), then an explicit restart (revision
    // bump + resume) at frame 80 once clip 7 has already completed once.
    cactus_headless_test::drive_frames(registry, kDt, 29);
    {
        const auto& log = log_at_x(registry, 0.0F);
        CHECK(log.marker_count == 1);
        CHECK(log.last_marker_name == "early");
        CHECK(log.finished_count == 0);
    }

    // Through the seek (frame 30) and up to just before the interruption
    // (frame 40): still no extra marker (the cue only exists at 0.05, long
    // behind the seek target) and still no completion (clip 3 interrupted
    // before reaching duration).
    for (int i = 0; i < 10; ++i) {
        cactus_headless_test::drive_frame(registry, kDt);
    }
    {
        const auto& log = log_at_x(registry, 0.0F);
        CHECK(log.marker_count == 1);
        CHECK(log.finished_count == 0);
    }

    // Clip 7 (Robot_Sitting, ~0.417s) has no authored cue on this owner, so no
    // more markers; ~25 frames after the frame-40 interruption it completes.
    for (int i = 0; i < 39; ++i) {
        cactus_headless_test::drive_frame(registry, kDt);
    }
    {
        const auto& log = log_at_x(registry, 0.0F);
        CHECK(log.marker_count == 1);
        CHECK(log.finished_count == 1);
        CHECK(log.finished_clip == 7);
        CHECK_FALSE(registry.get<std_render_models__ModelAnimator>(entity_at_x(registry, 0.0F)).playing);
    }

    // Frame 80's restart (revision bump + time reset + resume) rearms
    // completion; ~25 more frames completes clip 7 a second time.
    for (int i = 0; i < 41; ++i) {
        cactus_headless_test::drive_frame(registry, kDt);
    }
    const auto& log = log_at_x(registry, 0.0F);
    CHECK(log.finished_count == 2);
    CHECK(log.finished_clip == 7);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
