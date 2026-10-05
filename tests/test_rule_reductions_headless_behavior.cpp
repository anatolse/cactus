// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

namespace {

void run_one_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

const rule_reductions__Board& board_of(entt::registry& registry) {
    const auto view = registry.view<rule_reductions__Board>();
    REQUIRE(view.begin() != view.end());
    return registry.get<rule_reductions__Board>(*view.begin());
}

const rule_reductions__Team& team_of(entt::registry& registry, int id) {
    for (const auto entity : registry.view<rule_reductions__Team>()) {
        const auto& team = registry.get<rule_reductions__Team>(entity);
        if (team.id == id) {
            return team;
        }
    }
    FAIL("no team with id " << id);
    std::unreachable();
}

constexpr int RED_TEAM   = 1;
constexpr int BLUE_TEAM  = 2;
constexpr int GREEN_TEAM = 3;
constexpr int GOLD_TEAM  = 4;
constexpr int GRAY_TEAM  = 5;

}  // namespace

TEST_CASE("a global reduction over an empty domain runs once with identity values",
          "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto& board = board_of(registry);
    CHECK(board.enemy_passes == 1);
    CHECK(board.enemies == 0);
    CHECK(board.threat == 0);
    CHECK(board.weakest == 7);
}

TEST_CASE("a global reduction totals every row", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto& board = board_of(registry);
    CHECK(board.players == 5);
    CHECK(board.points == 21);
}

TEST_CASE("a vector sum adds per component", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto& push = board_of(registry).push;
    CHECK(push.x == 2.0F);
    CHECK(push.y == 2.0F);
    CHECK(push.z == -1.0F);
    const auto& red = team_of(registry, RED_TEAM).push;
    CHECK(red.x == 1.0F);
    CHECK(red.y == 2.0F);
    CHECK(red.z == 0.0F);
}

TEST_CASE("a grouped reduction totals each group's rows", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto& red = team_of(registry, RED_TEAM);
    CHECK(red.activations == 1);
    CHECK(red.players == 2);
    CHECK(red.total == 8);
    CHECK(red.best == 2.5F);
    const auto& blue = team_of(registry, BLUE_TEAM);
    CHECK(blue.players == 1);
    CHECK(blue.total == 4);
    CHECK(blue.best == 3.0F);
}

TEST_CASE("a group with no rows gets identity values", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto& green = team_of(registry, GREEN_TEAM);
    CHECK(green.activations == 1);
    CHECK(green.players == 0);
    CHECK(green.total == 0);
    CHECK(green.best == -1.0F);
    CHECK_FALSE(green.has_star);
    CHECK(green.push.x == 0.0F);
    CHECK(green.push.y == 0.0F);
    CHECK(green.push.z == 0.0F);
}

TEST_CASE("any is true only when some row matches", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    CHECK(team_of(registry, RED_TEAM).has_star);
    CHECK_FALSE(team_of(registry, BLUE_TEAM).has_star);
}

TEST_CASE("a where predicate on the group binding removes the group", "[runtime][codegen-entt][rule-reduce]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto& gray = team_of(registry, GRAY_TEAM);
    CHECK(gray.activations == 0);
    CHECK(gray.players == -1);
    CHECK(team_of(registry, GOLD_TEAM).activations == 1);
}

TEST_CASE("order by and limit rank aggregate rows with creation-order ties",
          "[runtime][codegen-entt][rule-reduce]") {
    // Red and Gold both total 8; Red was created first.
    entt::registry registry;
    run_one_frame(registry);
    CHECK(board_of(registry).ranked == 14);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
