// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cactus::runtime::entt_backend;

namespace {

struct TagComponent {};

struct DataComponent {
    int value{0};
};

}  // namespace

TEST_CASE("ProjectedTraitTracker (tag): project materializes the component, clear removes a project-only tag",
          "[runtime][projected-trait][tag]") {
    entt::registry registry;
    const auto entity = registry.create();

    ProjectedTraitTracker<TagComponent> tracker;
    tracker.project(registry, entity);
    CHECK(registry.all_of<TagComponent>(entity));

    tracker.clear(registry);
    CHECK_FALSE(registry.all_of<TagComponent>(entity));
}

TEST_CASE("ProjectedTraitTracker (tag): remember is idempotent so cancel keeps a later durable write",
          "[runtime][projected-trait][tag]") {
    entt::registry registry;
    const auto entity = registry.create();

    ProjectedTraitTracker<TagComponent> tracker;
    tracker.remember(registry, entity);
    tracker.remember(registry, entity);  // second call must not re-snapshot
    tracker.project(registry, entity);
    tracker.cancel(entity);  // a durable AddTrait now owns this component going forward

    tracker.clear(registry);
    // cancel() removed the tracking entry, so clear() must leave the
    // now-durable component untouched instead of erasing it.
    CHECK(registry.all_of<TagComponent>(entity));
}

TEST_CASE("ProjectedTraitTracker (data-bearing): project returns a mutable reference, clear removes a "
          "project-only component",
          "[runtime][projected-trait][data]") {
    entt::registry registry;
    const auto entity = registry.create();

    ProjectedTraitTracker<DataComponent> tracker;
    auto& projected = tracker.project(registry, entity);
    projected.value = 42;
    CHECK(registry.get<DataComponent>(entity).value == 42);

    tracker.clear(registry);
    CHECK_FALSE(registry.all_of<DataComponent>(entity));
}

TEST_CASE("ProjectedTraitTracker (data-bearing): project over a durable component restores the durable value "
          "at clear",
          "[runtime][projected-trait][data]") {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<DataComponent>(entity, DataComponent{.value = 10});

    ProjectedTraitTracker<DataComponent> tracker;
    tracker.project(registry, entity).value = 99;
    CHECK(registry.get<DataComponent>(entity).value == 99);

    tracker.clear(registry);
    REQUIRE(registry.all_of<DataComponent>(entity));
    CHECK(registry.get<DataComponent>(entity).value == 10);
}

TEST_CASE("ProjectedTraitTracker (data-bearing): repeated projection coalesces to one current value and "
          "restores the pre-first-projection snapshot",
          "[runtime][projected-trait][data]") {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<DataComponent>(entity, DataComponent{.value = 10});

    ProjectedTraitTracker<DataComponent> tracker;
    tracker.project(registry, entity).value = 20;
    tracker.project(registry, entity).value = 30;

    // Later matching observes exactly one current value: the latest write.
    CHECK(registry.get<DataComponent>(entity).value == 30);

    tracker.clear(registry);
    // Cleanup restores the value that existed before the *first* projection,
    // not the value from the intermediate (first) projection.
    REQUIRE(registry.all_of<DataComponent>(entity));
    CHECK(registry.get<DataComponent>(entity).value == 10);
}

TEST_CASE("ProjectedTraitTracker (tag): repeated projection with no pre-existing durable value coalesces to "
          "project-only removal at clear",
          "[runtime][projected-trait][tag]") {
    entt::registry registry;
    const auto entity = registry.create();

    ProjectedTraitTracker<TagComponent> tracker;
    tracker.project(registry, entity);
    tracker.project(registry, entity);
    CHECK(registry.all_of<TagComponent>(entity));

    tracker.clear(registry);
    CHECK_FALSE(registry.all_of<TagComponent>(entity));
}

TEST_CASE("ProjectedTraitTracker: projecting a stale/destroyed entity is a safe no-op", "[runtime][projected-trait]") {
    entt::registry registry;
    const auto stale_entity = registry.create();
    registry.destroy(stale_entity);

    ProjectedTraitTracker<TagComponent> tag_tracker;
    CHECK_NOTHROW(tag_tracker.remember(registry, stale_entity));
    CHECK_NOTHROW(tag_tracker.project(registry, stale_entity));
    CHECK_NOTHROW(tag_tracker.clear(registry));

    ProjectedTraitTracker<DataComponent> data_tracker;
    CHECK_NOTHROW(data_tracker.remember(registry, stale_entity));
    CHECK_NOTHROW(data_tracker.project(registry, stale_entity));
    CHECK_NOTHROW(data_tracker.clear(registry));

    // A stale project must not perturb a subsequent, genuinely live entity's tracking.
    const auto live_entity = registry.create();
    data_tracker.project(registry, live_entity).value = 7;
    CHECK(registry.get<DataComponent>(live_entity).value == 7);
    data_tracker.clear(registry);
    CHECK_FALSE(registry.all_of<DataComponent>(live_entity));
}

TEST_CASE("ProjectedTraitTracker: a projected entity destroyed mid-frame is skipped, not restored, at clear",
          "[runtime][projected-trait]") {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<DataComponent>(entity, DataComponent{.value = 5});

    ProjectedTraitTracker<DataComponent> tracker;
    tracker.project(registry, entity).value = 55;
    registry.destroy(entity);

    CHECK_NOTHROW(tracker.clear(registry));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
