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

#include <string>

namespace {

using cactus_headless_test::drive_frame;
using cactus_headless_test::init_and_load;

namespace rt = cactus::runtime::entt_backend;

using Blessed = world_persistence_dynamic__Blessed;
using Health  = world_persistence_dynamic__Health;
using Sparkle = world_persistence_dynamic__Sparkle;
using Stage   = world_persistence_dynamic__Stage;

constexpr float kStep = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        rt::generated_scheduler_state().std_core__fixed_tick = {};
        init_and_load(registry);
    }
};

template <typename Trait>
entt::entity entity_with(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    REQUIRE(view.begin() != view.end());
    return *view.begin();
}

Stage& stage(entt::registry& registry) {
    return registry.get<Stage>(entity_with<Stage>(registry));
}

std::string origin_of(entt::registry& registry, entt::entity entity) {
    REQUIRE(rt::has_capture_provenance(registry, entity));
    return std::string(rt::generated_archetype_nodes[registry.get<rt::ArchetypeOrigin>(entity).node]);
}

// Runs one frame for each queued stage step, so every emitted event reaches its
// handler and commits before the next one is queued.
void run_frames(entt::registry& registry, int count) {
    for (int frame = 0; frame < count; ++frame) {
        drive_frame(registry, kStep);
    }
}

entt::entity spawn_ember(entt::registry& registry) {
    stage(registry).pending_embers = 1;
    run_frames(registry, 1);
    return entity_with<Sparkle>(registry);
}

cactus::persistence::Snapshot capture(entt::registry& registry) {
    return rt::generated_capture_world_snapshot(registry);
}

}  // namespace

TEST_CASE("an ember records the values it was actually spawned with", "[runtime][persistence][provenance]") {
    World world;
    auto& registry     = world.registry;
    const auto ember   = spawn_ember(registry);
    const auto& source = registry.get<rt::Construction<Sparkle>>(ember).value;

    CHECK(origin_of(registry, ember) == "world_persistence_dynamic.Ember");
    // 0.5 comes from the archetype, 0.7 from the spawn override — both are
    // evaluated once at construction.
    CHECK(source.lifetime == Catch::Approx(0.5F));
    CHECK(source.hue == Catch::Approx(0.7F));
}

TEST_CASE("an ephemeral archetype keeps provenance because the program can make it eligible",
          "[runtime][persistence][provenance]") {
    World world;
    auto& registry   = world.registry;
    const auto ember = spawn_ember(registry);

    registry.get<Sparkle>(ember).hue = 0.1F;

    stage(registry).awaken = 1;
    run_frames(registry, 1);

    REQUIRE(registry.all_of<Health>(ember));
    // Now eligible, and its original construction values are still the ones a
    // save would rebuild it from.
    CHECK(registry.get<rt::Construction<Sparkle>>(ember).value.hue == Catch::Approx(0.7F));
}

TEST_CASE("a trait added at runtime records its initialization", "[runtime][persistence][provenance]") {
    World world;
    auto& registry   = world.registry;
    const auto ember = spawn_ember(registry);

    stage(registry).awaken = 1;
    run_frames(registry, 1);

    REQUIRE(registry.all_of<Health>(ember));
    CHECK(registry.get<rt::Construction<Health>>(ember).value.current == 42);
    // Health's own default for the field the add never mentions.
    CHECK(registry.get<rt::Construction<Health>>(ember).value.maximum == 100);
}

TEST_CASE("a marker trait is membership only, with no construction payload",
          "[runtime][persistence][provenance]") {
    World world;
    auto& registry   = world.registry;
    const auto ember = spawn_ember(registry);

    stage(registry).awaken = 1;
    run_frames(registry, 1);

    CHECK(registry.all_of<Blessed>(ember));
    CHECK_FALSE(registry.any_of<rt::Construction<Blessed>>(ember));
}

TEST_CASE("removing and re-adding a trait records the new incarnation", "[runtime][persistence][provenance]") {
    World world;
    auto& registry   = world.registry;
    const auto ember = spawn_ember(registry);

    stage(registry).awaken = 1;
    run_frames(registry, 1);
    REQUIRE(registry.get<rt::Construction<Health>>(ember).value.current == 42);

    stage(registry).wither = 1;
    run_frames(registry, 1);
    REQUIRE_FALSE(registry.all_of<Health>(ember));
    CHECK_FALSE(registry.any_of<rt::Construction<Health>>(ember));

    stage(registry).rekindle = 1;
    run_frames(registry, 1);

    REQUIRE(registry.all_of<Health>(ember));
    CHECK(registry.get<rt::Construction<Health>>(ember).value.current == 7);
}

TEST_CASE("an archetype keeps its eligibility provenance after its durable trait is removed",
          "[runtime][persistence][provenance]") {
    World world;
    auto& registry  = world.registry;
    const auto boss = entity_with<Health>(registry);

    REQUIRE(origin_of(registry, boss) == "world_persistence_dynamic.Boss");
    REQUIRE(registry.get<rt::Construction<Health>>(boss).value.maximum == 500);

    stage(registry).wither = 1;
    run_frames(registry, 1);

    REQUIRE_FALSE(registry.all_of<Health>(boss));
    CHECK(rt::has_capture_provenance(registry, boss));
    CHECK(origin_of(registry, boss) == "world_persistence_dynamic.Boss");
}

TEST_CASE("an entity created outside any archetype path has no capture provenance",
          "[runtime][persistence][provenance]") {
    World world;
    auto& registry = world.registry;

    const auto bare = registry.create();
    CHECK_FALSE(rt::has_capture_provenance(registry, bare));

    registry.destroy(bare);
    CHECK_FALSE(rt::has_capture_provenance(registry, bare));
}

TEST_CASE("a durable trait on an entity with no provenance is reported, not silently saved",
          "[runtime][persistence][provenance]") {
    World world;
    auto& registry = world.registry;

    // Every entity the DSL created carries provenance.
    CHECK_FALSE(rt::generated_entity_missing_provenance(registry).has_value());

    const auto bare = registry.create();
    registry.emplace<Health>(bare, Health{});

    const auto missing = rt::generated_entity_missing_provenance(registry);
    REQUIRE(missing.has_value());
    CHECK(*missing == bare);
}

// ── World restore (add-world-snapshot-restore) ──────────────────────────────

TEST_CASE("restoring a marker trait recreates membership with no payload", "[runtime][persistence][restore]") {
    World world;
    auto& registry = world.registry;
    spawn_ember(registry);

    stage(registry).awaken = 1;
    run_frames(registry, 1);
    REQUIRE(registry.all_of<Blessed>(entity_with<Sparkle>(registry)));

    const auto snapshot = capture(registry);
    const auto outcome  = rt::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto restored_ember = entity_with<Sparkle>(registry);
    CHECK(registry.all_of<Blessed>(restored_ember));
    CHECK(registry.get<Health>(restored_ember).current == 42);
}

TEST_CASE("restoring after remove-and-readd uses the recorded incarnation, not the removed one",
          "[runtime][persistence][restore]") {
    World world;
    auto& registry = world.registry;
    spawn_ember(registry);

    stage(registry).awaken = 1;
    run_frames(registry, 1);
    stage(registry).wither = 1;
    run_frames(registry, 1);
    stage(registry).rekindle = 1;
    run_frames(registry, 1);
    REQUIRE(registry.get<Health>(entity_with<Sparkle>(registry)).current == 7);

    const auto snapshot = capture(registry);
    const auto outcome  = rt::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto restored_ember = entity_with<Sparkle>(registry);
    REQUIRE(registry.all_of<Health>(restored_ember));
    CHECK(registry.get<Health>(restored_ember).current == 7);
}

TEST_CASE("restoring a removed baseline trait leaves it absent", "[runtime][persistence][restore]") {
    World world;
    auto& registry  = world.registry;
    const auto boss = entity_with<Health>(registry);
    REQUIRE(origin_of(registry, boss) == "world_persistence_dynamic.Boss");

    stage(registry).wither = 1;
    run_frames(registry, 1);
    REQUIRE_FALSE(registry.all_of<Health>(boss));

    const auto snapshot = capture(registry);
    const auto outcome  = rt::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto view = registry.view<rt::ArchetypeOrigin>();
    entt::entity restored_boss = entt::null;
    for (const auto entity : view) {
        if (origin_of(registry, entity) == "world_persistence_dynamic.Boss") {
            restored_boss = entity;
        }
    }
    REQUIRE(restored_boss != entt::entity{entt::null});
    CHECK_FALSE(registry.all_of<Health>(restored_boss));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
