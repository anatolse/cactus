// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "common/persistence_validation.hpp"
#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_snapshot_edit.hpp"

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

bool has_record_for(const cactus::persistence::Snapshot& snapshot, const std::string& archetype) {
    return std::ranges::any_of(snapshot.entities(), [&](const cactus::persistence::EntityView& record) {
        return record.archetype() == archetype;
    });
}

// The child first, then its parent.
cactus::persistence::Snapshot child_first(const cactus::persistence::Snapshot& snapshot) {
    return cactus_test::edited(snapshot, [](std::vector<cactus_test::EditableRecord>& records) {
        REQUIRE(records.size() == 2);
        std::swap(records[0], records[1]);
    });
}

}  // namespace

TEST_CASE("a destroyed hierarchical child is not captured", "[runtime][persistence][restore][hierarchy]") {
    World world;
    const auto snapshot = capture(world.registry);

    CHECK_FALSE(has_record_for(snapshot, "persistence_hierarchy_runtime.Leader/Doomed"));
    CHECK(has_record_for(snapshot, "persistence_hierarchy_runtime.Leader"));
    CHECK(has_record_for(snapshot, "persistence_hierarchy_runtime.Leader/Follower"));
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
    REQUIRE(snapshot.entity_count() == 2);
    CHECK(snapshot.entity(0).archetype() == "persistence_hierarchy_runtime.Leader");
    CHECK(snapshot.entity(1).archetype() == "persistence_hierarchy_runtime.Leader/Follower");

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto recaptured = capture(registry);
    REQUIRE(recaptured.entity_count() == 2);
    CHECK(recaptured.entity(0).archetype() == "persistence_hierarchy_runtime.Leader");
    CHECK(recaptured.entity(1).archetype() == "persistence_hierarchy_runtime.Leader/Follower");
}

TEST_CASE("a child recorded before its parent is rejected and the world is unchanged",
          "[runtime][persistence][restore][hierarchy-forest]") {
    World world;
    auto& registry = world.registry;
    const auto snapshot = child_first(capture(registry));
    const auto child    = snapshot.entity(0);
    REQUIRE(child.archetype() == "persistence_hierarchy_runtime.Leader/Follower");

    const auto errors = cactus::persistence::validate_document(cactus::runtime::entt_backend::generated_schema, snapshot);
    const auto child_path = std::string(child.archetype()) + "[" + std::to_string(child.id()) + "]";
    CHECK(std::ranges::any_of(errors, [&](const cactus::persistence::ValidationError& error) {
        return error.field_path == child_path && error.message == "child recorded before its parent";
    }));

    const auto before  = capture(registry);
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "invalid_data");
    const auto after = capture(registry);
    REQUIRE(after.entity_count() == before.entity_count());
    for (std::size_t index = 0; index < after.entity_count(); ++index) {
        CHECK(after.entity(index).archetype() == before.entity(index).archetype());
        CHECK(after.entity(index).parent() == before.entity(index).parent());
    }
}

TEST_CASE("a program that reparents accepts a child recorded before its parent",
          "[runtime][persistence][restore][hierarchy-forest]") {
    World world;
    const auto snapshot = child_first(capture(world.registry));
    auto schema          = cactus::runtime::entt_backend::generated_schema;
    schema.parents_first = false;
    CHECK(cactus::persistence::validate_document(schema, snapshot).empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
