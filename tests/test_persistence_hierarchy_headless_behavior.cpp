// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "common/persistence_validation.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace {

using cactus_headless_test::init_and_load;

using Health = cactus_gen::persistence_hierarchy_runtime::Health;
using Parent = cactus_gen::stdlib::core::Parent;

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

TEST_CASE("a child recorded before its parent is rejected and the world is unchanged",
          "[runtime][persistence][restore][hierarchy-forest]") {
    World world;
    auto& registry = world.registry;
    auto snapshot  = capture(registry);
    REQUIRE(snapshot.entities.size() == 2);
    std::swap(snapshot.entities[0], snapshot.entities[1]);
    const auto& child = snapshot.entities[0];
    REQUIRE(child.archetype == "persistence_hierarchy_runtime.Leader/Follower");

    const auto errors = cactus::persistence::validate_document(cactus::runtime::entt_backend::generated_schema, snapshot);
    const auto child_path = child.archetype + "[" + std::to_string(child.id) + "]";
    CHECK(std::ranges::any_of(errors, [&](const cactus::persistence::ValidationError& error) {
        return error.field_path == child_path && error.message == "child recorded before its parent";
    }));

    const auto before  = capture(registry);
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "invalid_data");
    const auto after = capture(registry);
    REQUIRE(after.entities.size() == before.entities.size());
    for (std::size_t index = 0; index < after.entities.size(); ++index) {
        CHECK(after.entities[index].archetype == before.entities[index].archetype);
        CHECK(after.entities[index].parent == before.entities[index].parent);
    }
}

TEST_CASE("a program that reparents accepts a child recorded before its parent",
          "[runtime][persistence][restore][hierarchy-forest]") {
    World world;
    auto snapshot = capture(world.registry);
    REQUIRE(snapshot.entities.size() == 2);
    std::swap(snapshot.entities[0], snapshot.entities[1]);
    auto schema          = cactus::runtime::entt_backend::generated_schema;
    schema.parents_first = false;
    CHECK(cactus::persistence::validate_document(schema, snapshot).empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
