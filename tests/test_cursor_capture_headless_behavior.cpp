// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("cursor capture headless: capture, repeat, and release are observable and idempotent",
          "[runtime][codegen-entt][input][cursor]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_mouse_delta(Vector2{.x = 4.0F, .y = -3.0F});

    entt::registry registry;
    cactus_headless_test::drive_one_frame(registry, 1.0F / 60.0F);
    const auto sequences = registry.view<cursor_capture_runtime__CursorSequence>();
    REQUIRE(sequences.size() == 1);
    const auto& sequence = registry.get<cursor_capture_runtime__CursorSequence>(*sequences.begin());
    CHECK(sequence.observed_delta.x == 4.0F);
    CHECK(sequence.observed_delta.y == -3.0F);
    CHECK(cactus_raylib_fake::cursor_captured());
    REQUIRE(cactus_raylib_fake::cursor_capture_transitions().size() == 1);
    CHECK(cactus_raylib_fake::cursor_capture_transitions().front());

    cactus_headless_test::drive_frame(registry);
    CHECK(cactus_raylib_fake::cursor_captured());
    CHECK(cactus_raylib_fake::cursor_capture_transitions().size() == 1);

    cactus_headless_test::drive_frame(registry);
    CHECK_FALSE(cactus_raylib_fake::cursor_captured());
    REQUIRE(cactus_raylib_fake::cursor_capture_transitions().size() == 2);
    CHECK_FALSE(cactus_raylib_fake::cursor_capture_transitions().back());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
