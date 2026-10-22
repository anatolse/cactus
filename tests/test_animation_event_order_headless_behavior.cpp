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

TEST_CASE("animation events of one tick arrive in owner creation order",
          "[runtime][dsl-model-animation-events][native-result-ordering]") {
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);

    entt::registry registry;
    cactus_headless_test::drive_frames(registry, 1.0F / 60.0F, 10);

    const auto recorder = cactus_gen::detail::generated_named_slots().animation_event_order.Recorder;
    CHECK(registry.get<cactus_gen::animation_event_order::Arrivals>(recorder).digits == 123);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
