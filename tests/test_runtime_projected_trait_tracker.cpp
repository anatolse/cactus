// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"
#include "heap_allocation_counter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

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

TEST_CASE("ProjectedTraitTracker: durable_value under a projection is the remembered value, so a patch "
          "survives clear without changing the overlay",
          "[runtime][projected-trait][deferred-set]") {
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<DataComponent>(entity, DataComponent{.value = 10});

    ProjectedTraitTracker<DataComponent> tracker;
    tracker.project(registry, entity).value = 99;

    auto* durable = tracker.durable_value(registry, entity);
    REQUIRE(durable != nullptr);
    durable->value = 7;
    CHECK(registry.get<DataComponent>(entity).value == 99);

    tracker.clear(registry);
    CHECK(registry.get<DataComponent>(entity).value == 7);
}

TEST_CASE("ProjectedTraitTracker: durable_value is null for a projection-only component",
          "[runtime][projected-trait][deferred-set]") {
    entt::registry registry;
    const auto entity = registry.create();

    ProjectedTraitTracker<DataComponent> tracker;
    tracker.project(registry, entity).value = 99;

    CHECK(tracker.durable_value(registry, entity) == nullptr);
}

TEST_CASE("ProjectedTraitTracker: durable_value without a projection is the live component, or null when absent",
          "[runtime][projected-trait][deferred-set]") {
    entt::registry registry;
    const auto with_component    = registry.create();
    const auto without_component = registry.create();
    registry.emplace<DataComponent>(with_component, DataComponent{.value = 10});

    ProjectedTraitTracker<DataComponent> tracker;
    CHECK(tracker.durable_value(registry, with_component) == &registry.get<DataComponent>(with_component));
    CHECK(tracker.durable_value(registry, without_component) == nullptr);

    registry.destroy(with_component);
    CHECK(tracker.durable_value(registry, with_component) == nullptr);
}

TEST_CASE("ProjectedTraitTracker: projecting onto the same entities each frame stops allocating",
          "[runtime][projected-trait][data]") {
    entt::registry registry;
    std::vector<entt::entity> entities(150);
    registry.create(entities.begin(), entities.end());
    for (std::size_t i = 0; i < entities.size(); i += 2) {
        registry.emplace<DataComponent>(entities[i], DataComponent{.value = static_cast<int>(i)});
    }

    ProjectedTraitTracker<DataComponent> tracker;
    const auto frame = [&] {
        for (const auto entity : entities) {
            tracker.project(registry, entity).value = -1;
        }
        tracker.clear(registry);
    };
    frame();
    std::size_t allocations = 0;
    {
        const cactus_test::AllocationCount counting;
        frame();
        frame();
        allocations = cactus_test::AllocationCount::count();
    }
    CHECK(allocations == 0);
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (i % 2 == 0) {
            CHECK(registry.get<DataComponent>(entities[i]).value == static_cast<int>(i));
        } else {
            CHECK_FALSE(registry.all_of<DataComponent>(entities[i]));
        }
    }
}

TEST_CASE("ProjectedTraitTracker: cleanup restores durable values for entities that gained, lost or were reused",
          "[runtime][projected-trait][data]") {
    entt::registry registry;
    const auto durable = registry.create();
    registry.emplace<DataComponent>(durable, DataComponent{.value = 5});
    const auto gained    = registry.create();
    const auto cancelled = registry.create();
    const auto reused    = registry.create();

    ProjectedTraitTracker<DataComponent> tracker;
    for (int frame = 0; frame < 2; ++frame) {
        tracker.project(registry, durable).value = 50;
        registry.remove<DataComponent>(durable);  // the projection lost the trait mid-frame
        tracker.project(registry, gained).value = 60;
        tracker.project(registry, cancelled).value = 70;
        tracker.cancel(cancelled);
        CHECK_FALSE(tracker.is_projected(cancelled));
        tracker.project(registry, reused).value = 80;
        CHECK(tracker.is_projected(reused));
        tracker.clear(registry);

        REQUIRE(registry.all_of<DataComponent>(durable));
        CHECK(registry.get<DataComponent>(durable).value == 5);
        CHECK_FALSE(registry.all_of<DataComponent>(gained));
        CHECK(registry.get<DataComponent>(cancelled).value == 70);
        CHECK_FALSE(registry.all_of<DataComponent>(reused));
        CHECK_FALSE(tracker.is_projected(durable));
        registry.remove<DataComponent>(cancelled);
    }

    tracker.project(registry, reused).value = 1;
    registry.destroy(reused);
    const auto successor = registry.create();
    REQUIRE(entt::to_entity(successor) == entt::to_entity(reused));
    CHECK_FALSE(tracker.is_projected(successor));
    registry.emplace<DataComponent>(successor, DataComponent{.value = 9});
    tracker.project(registry, successor).value = 90;
    CHECK(tracker.durable_value(registry, successor)->value == 9);
    tracker.clear(registry);
    CHECK(registry.get<DataComponent>(successor).value == 9);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
