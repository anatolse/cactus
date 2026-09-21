// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

namespace {

using cactus_headless_test::init_and_load;

using Health = persistence_hierarchy_runtime__Health;
using Parent = std_core__Parent;

struct World {
    entt::registry registry;

    World() { init_and_load(registry); }
};

cactus::persistence::Snapshot capture(entt::registry& registry) {
    return cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
}

const cactus::persistence::EntityRecord* record_for_archetype(const cactus::persistence::Snapshot& snapshot,
                                                               const std::string& archetype) {
    const auto found = std::ranges::find(snapshot.entities, archetype, &cactus::persistence::EntityRecord::archetype);
    return found == snapshot.entities.end() ? nullptr : &*found;
}

}  // namespace

TEST_CASE("a destroyed hierarchical child is not captured", "[runtime][persistence][restore][hierarchy]") {
    World world;
    const auto snapshot = capture(world.registry);

    CHECK(record_for_archetype(snapshot, "persistence_hierarchy_runtime.Leader/Doomed") == nullptr);
    CHECK(record_for_archetype(snapshot, "persistence_hierarchy_runtime.Leader") != nullptr);
    CHECK(record_for_archetype(snapshot, "persistence_hierarchy_runtime.Leader/Follower") != nullptr);
}

TEST_CASE("restoring an included parent link resolves to the restored parent entity",
          "[runtime][persistence][restore][hierarchy]") {
    World world;
    auto& registry = world.registry;

    const auto snapshot = capture(registry);
    const auto outcome  = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    entt::entity leader   = entt::null;
    entt::entity follower = entt::null;
    for (const auto entity : registry.view<Health>()) {
        if (registry.get<Health>(entity).current == 500) {
            leader = entity;
        } else if (registry.get<Health>(entity).current == 50) {
            follower = entity;
        }
    }
    REQUIRE(leader != entt::entity{entt::null});
    REQUIRE(follower != entt::entity{entt::null});

    REQUIRE(registry.all_of<Parent>(follower));
    CHECK(registry.get<Parent>(follower).parent == leader);
    CHECK_FALSE(registry.all_of<Parent>(leader));
}

TEST_CASE("a destroyed hierarchical child does not return after restore",
          "[runtime][persistence][restore][hierarchy]") {
    World world;
    auto& registry = world.registry;

    const auto snapshot = capture(registry);
    const auto outcome  = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    // Only Leader (500) and Follower (50) restore; Doomed (1) was destroyed
    // before capture and must not reappear.
    for (const auto entity : registry.view<Health>()) {
        CHECK(registry.get<Health>(entity).current != 1);
    }
    CHECK(std::ranges::distance(registry.view<Health>().begin(), registry.view<Health>().end()) == 2);
}

TEST_CASE("restore preserves relative creation order among restored entities",
          "[runtime][persistence][restore][hierarchy]") {
    World world;
    auto& registry = world.registry;

    const auto snapshot = capture(registry);
    REQUIRE(snapshot.entities.size() == 2);
    CHECK(snapshot.entities[0].archetype == "persistence_hierarchy_runtime.Leader");
    CHECK(snapshot.entities[1].archetype == "persistence_hierarchy_runtime.Leader/Follower");

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto recaptured = capture(registry);
    REQUIRE(recaptured.entities.size() == 2);
    CHECK(recaptured.entities[0].archetype == "persistence_hierarchy_runtime.Leader");
    CHECK(recaptured.entities[1].archetype == "persistence_hierarchy_runtime.Leader/Follower");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
