// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

void run_one_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    drive_frame(registry);
}

const team_score_reduce_runtime__Team& team_of(entt::registry& registry, int id) {
    for (const auto entity : registry.view<team_score_reduce_runtime__Team>()) {
        const auto& team = registry.get<team_score_reduce_runtime__Team>(entity);
        if (team.id == id) {
            return team;
        }
    }
    FAIL("no team with id " << id);
    std::unreachable();
}

constexpr int RED_TEAM  = 1;
constexpr int BLUE_TEAM = 2;

}  // namespace

TEST_CASE("a team without members still gets one activation", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto& blue = team_of(registry, BLUE_TEAM);
    CHECK(blue.activations == 1);
    CHECK(blue.members == 0);
    CHECK(blue.score == 0);
}

TEST_CASE("when the last member leaves the team gets count 0", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    CHECK(team_of(registry, RED_TEAM).members == 1);
    CHECK(team_of(registry, RED_TEAM).score == 5);

    const auto members = registry.view<team_score_reduce_runtime__Member>();
    registry.destroy(members.begin(), members.end());
    drive_frame(registry);

    const auto& red = team_of(registry, RED_TEAM);
    CHECK(red.activations == 2);
    CHECK(red.members == 0);
    CHECK(red.score == 0);
}

TEST_CASE("aggregates come from the input snapshot, not from earlier handlers",
          "[runtime][codegen-entt][rule-reduce]") {
    // Red's handler raises Red's bonus to 11 before Blue's handler runs.
    // Blue still sees Red's bonus as 1.
    entt::registry registry;
    run_one_frame(registry);
    CHECK(team_of(registry, RED_TEAM).rival_bonus == 2);
    CHECK(team_of(registry, BLUE_TEAM).rival_bonus == 1);
    CHECK(team_of(registry, RED_TEAM).bonus == 11);
}
TEST_CASE("int sums saturate per step and float sums keep fold order", "[runtime][codegen-entt][rule-reduce]") {
    // 2147483647 + 1 clamps, then -1 gives 2147483646. (1e8 + 1) - 1e8 is 0
    // in float; a reassociated sum would give 1.
    entt::registry registry;
    run_one_frame(registry);
    const auto view = registry.view<team_score_reduce_runtime__Totals>();
    REQUIRE(view.begin() != view.end());
    const auto& totals = registry.get<team_score_reduce_runtime__Totals>(*view.begin());
    CHECK(totals.whole == 2147483646);
    CHECK(totals.part == 0.0F);
}
TEST_CASE("a targeted event runs only the recipient's group", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    CHECK(team_of(registry, RED_TEAM).recounts == 1);
    CHECK(team_of(registry, RED_TEAM).recount_members == 1);
    CHECK(team_of(registry, BLUE_TEAM).recounts == 0);
}
TEST_CASE("a global reduction ignores the event target", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto& totals = registry.get<team_score_reduce_runtime__Totals>(
        *registry.view<team_score_reduce_runtime__Totals>().begin());
    CHECK(totals.recounts == 1);
    CHECK(totals.recount_members == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
