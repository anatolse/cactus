// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using Match  = bracketed_trait_access__Match;
using Runner = bracketed_trait_access__Runner;
using Timer  = std_time__Timer;

using cactus_headless_test::drive_frame;

template <typename Trait>
entt::entity only_entity(entt::registry& registry) {
    auto view = registry.view<Trait>();
    REQUIRE(view.size() == 1);
    return *view.begin();
}

int next_lane_of(entt::registry& registry, int lane) {
    for (const auto entity : registry.view<Runner>()) {
        const auto& runner = registry.get<Runner>(entity);
        if (runner.lane == lane) {
            return runner.next_lane;
        }
    }
    FAIL("no runner in lane " << lane);
    return 0;
}

}  // namespace

TEST_CASE("named bracketed reads and writes drive game-wide state", "[runtime][trait-access]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto game = only_entity<Match>(registry);

    drive_frame(registry);
    CHECK(registry.get<Match>(game).ticks == 1);
    CHECK_FALSE(registry.get<Match>(game).over);

    for (int frame = 0; frame < 4; ++frame) {
        drive_frame(registry);
    }
    CHECK(registry.get<Match>(game).ticks == 3);
    CHECK(registry.get<Match>(game).over);
}

TEST_CASE("an imported trait is written through a named entity", "[runtime][trait-access]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto clock = only_entity<Timer>(registry);

    drive_frame(registry);
    drive_frame(registry);
    CHECK(registry.get<Timer>(clock).remaining == 4.0F);
    CHECK(registry.get<Timer>(clock).paused);
}

TEST_CASE("pair rules read through brackets and bare aliases", "[runtime][trait-access]") {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    const auto game = only_entity<Match>(registry);

    drive_frame(registry);
    CHECK(next_lane_of(registry, 1) == 4);
    CHECK(next_lane_of(registry, 4) == 9);
    CHECK(next_lane_of(registry, 9) == -1);
    // Every runner is the rival of the two others: 2 * (1 + 4 + 9).
    CHECK(registry.get<Match>(game).score == 28);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
