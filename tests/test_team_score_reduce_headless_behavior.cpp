// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <exception>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(
        cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

void run_one_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    drive_frame(registry);
}

const cactus_gen::team_score_reduce_runtime::Team& team_of(entt::registry& registry, int id) {
    for (const auto entity : registry.view<cactus_gen::team_score_reduce_runtime::Team>()) {
        const auto& team = registry.get<cactus_gen::team_score_reduce_runtime::Team>(entity);
        if (team.id == id) {
            return team;
        }
    }
    FAIL("no team with id " << id);
    std::terminate();
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

    const auto members = registry.view<cactus_gen::team_score_reduce_runtime::Member>();
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
TEST_CASE("int sums wrap and float sums are exact", "[runtime][codegen-entt][rule-reduce]") {
    // 2147483647 + 1 - 1 wraps back to 2147483647 in any order. 1e8 + 1 - 1e8 is exactly 1.
    entt::registry registry;
    run_one_frame(registry);
    const auto view = registry.view<cactus_gen::team_score_reduce_runtime::Totals>();
    REQUIRE(view.begin() != view.end());
    const auto& totals = registry.get<cactus_gen::team_score_reduce_runtime::Totals>(*view.begin());
    CHECK(totals.whole == 2147483647);
    CHECK(totals.part == 1.0F);
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
    const auto& totals = registry.get<cactus_gen::team_score_reduce_runtime::Totals>(
        *registry.view<cactus_gen::team_score_reduce_runtime::Totals>().begin());
    CHECK(totals.recounts == 1);
    CHECK(totals.recount_members == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
