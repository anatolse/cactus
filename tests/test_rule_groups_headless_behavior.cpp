// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

using Body     = rule_groups__Body;
using Observed = rule_groups__Observed;

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry.
TEST_CASE("rules ordered against a group run before and after its member", "[runtime][codegen-entt][rule-groups]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto ball = generated_named_slots().rule_groups__Ball;

    for (int frame = 1; frame <= 3; ++frame) {
        cactus_headless_test::drive_frame(registry);
        const auto& body = registry.get<Body>(ball);
        INFO("frame " << frame << ", fixed steps " << body.steps);
        REQUIRE(body.steps >= frame);
        // The writer runs first in each step, so the member copies this step's value.
        CHECK(body.velocity == Catch::Approx(static_cast<float>(body.steps)));
        CHECK(body.thrust == Catch::Approx(body.velocity));
        CHECK(registry.get<Observed>(ball).seen == Catch::Approx(body.velocity));
    }

    cactus_headless_test::dispatch_boundary_event(registry, std_core__unloadEvent{});
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
