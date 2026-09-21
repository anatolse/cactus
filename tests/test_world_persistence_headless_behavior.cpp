// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "backends/cpp-entt/persistence_file_adapter.hpp"
#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using cactus_headless_test::drive_frame;
using cactus_headless_test::init_and_load;

constexpr float kStep = 1.0F / 60.0F;

struct World {
    entt::registry registry;

    World() {
        cactus::runtime::entt_backend::generated_scheduler_state().std_core__fixed_tick = {};
        init_and_load(registry);
    }
};

template <typename Trait>
entt::entity entity_with(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    REQUIRE(view.begin() != view.end());
    return *view.begin();
}

// Asks the director to create one enemy and one particle on the next frame.
void queue_spawns(entt::registry& registry, int enemies, int particles) {
    auto& spawner              = registry.get<world_persistence__Spawner>(
        entity_with<world_persistence__Spawner>(registry));
    spawner.pending_enemies    = enemies;
    spawner.pending_particles  = particles;
}

const cactus::persistence::EntityRecord* record_for_archetype(const cactus::persistence::Snapshot& snapshot,
                                                              const std::string& archetype) {
    const auto found = std::find_if(snapshot.entities.begin(),
                                    snapshot.entities.end(),
                                    [&](const cactus::persistence::EntityRecord& record) {
                                        return record.archetype == archetype;
                                    });
    return found == snapshot.entities.end() ? nullptr : &*found;
}

std::size_t count_for_archetype(const cactus::persistence::Snapshot& snapshot, const std::string& archetype) {
    return static_cast<std::size_t>(std::count_if(snapshot.entities.begin(),
                                                  snapshot.entities.end(),
                                                  [&](const cactus::persistence::EntityRecord& record) {
                                                      return record.archetype == archetype;
                                                  }));
}

const cactus::persistence::TraitRecord* trait_of(const cactus::persistence::EntityRecord& record,
                                                 const std::string& trait) {
    const auto found = std::find_if(record.traits.begin(),
                                    record.traits.end(),
                                    [&](const cactus::persistence::TraitRecord& candidate) {
                                        return candidate.trait == trait;
                                    });
    return found == record.traits.end() ? nullptr : &*found;
}

int int_field(const std::vector<cactus::persistence::FieldValue>& fields, const std::string& name) {
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const cactus::persistence::FieldValue& field) {
        return field.name == name;
    });
    REQUIRE(found != fields.end());
    return found->value.as_int();
}

float float_field(const std::vector<cactus::persistence::FieldValue>& fields, const std::string& name) {
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const cactus::persistence::FieldValue& field) {
        return field.name == name;
    });
    REQUIRE(found != fields.end());
    return found->value.as_float();
}

cactus::persistence::Snapshot capture(entt::registry& registry) {
    return cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
}

}  // namespace

TEST_CASE("a world snapshot records the boss archetype baseline rather than trait defaults",
          "[runtime][persistence][capture]") {
    World world;
    const auto snapshot = capture(world.registry);

    const auto* boss = record_for_archetype(snapshot, "world_persistence.Boss");
    REQUIRE(boss != nullptr);

    const auto* health = trait_of(*boss, "world_persistence.Health");
    REQUIRE(health != nullptr);
    // Health defaults maximum to 100; the Boss declaration raises it to 500, and
    // that 500 belongs to the archetype baseline, not to construction data.
    CHECK(int_field(health->construction, "maximum") == 500);
    CHECK(int_field(health->persisted, "current") == 500);
}

TEST_CASE("a world snapshot records the latest value of an explicitly persistent field",
          "[runtime][persistence][capture]") {
    World world;
    auto& registry = world.registry;

    const auto boss_entity = entity_with<world_persistence__Health>(registry);
    registry.get<world_persistence__Health>(boss_entity).current = 137;

    const auto snapshot = capture(registry);
    const auto* boss    = record_for_archetype(snapshot, "world_persistence.Boss");
    REQUIRE(boss != nullptr);
    const auto* health = trait_of(*boss, "world_persistence.Health");
    REQUIRE(health != nullptr);
    CHECK(int_field(health->persisted, "current") == 137);
}

TEST_CASE("a world snapshot records an enemy's original spawn argument, not its later mutation",
          "[runtime][persistence][capture]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 1, 0);
    drive_frame(registry, kStep);

    // Find the spawned enemy: the boss carries Health but no LocalTransform.
    const auto view = registry.view<world_persistence__Health, std_transform_flat__LocalTransform>();
    REQUIRE(view.begin() != view.end());
    const auto enemy = *view.begin();

    // ENEMY_MAXIMUM (700) was evaluated once at construction. Mutating the
    // unmarked field afterwards must not change what the save records.
    REQUIRE(registry.get<world_persistence__Health>(enemy).maximum == 700);
    registry.get<world_persistence__Health>(enemy).maximum = 900;
    registry.get<world_persistence__Health>(enemy).current = 42;

    const auto snapshot = capture(registry);
    const auto* record  = record_for_archetype(snapshot, "world_persistence.Enemy");
    REQUIRE(record != nullptr);
    const auto* health = trait_of(*record, "world_persistence.Health");
    REQUIRE(health != nullptr);
    CHECK(int_field(health->construction, "maximum") == 700);
    CHECK(int_field(health->persisted, "current") == 42);
}

TEST_CASE("customized particles stay out of a world snapshot", "[runtime][persistence][capture]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 0, 3);
    drive_frame(registry, kStep);
    drive_frame(registry, kStep);
    drive_frame(registry, kStep);

    REQUIRE(registry.view<world_persistence__Sparkle>().size() == 3);

    const auto snapshot = capture(registry);
    // Every particle carries spawn overrides and none carries a persist field,
    // so the document names none of them.
    CHECK(count_for_archetype(snapshot, "world_persistence.Particle") == 0);
    CHECK(count_for_archetype(snapshot, "world_persistence.Boss") == 1);
}

TEST_CASE("a scene survivor with no persist field is not automatically eligible",
          "[runtime][persistence][capture]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 0, 1);
    drive_frame(registry, kStep);

    // Particle carries std.core.Persistent (scene survival) but no persist
    // field anywhere in its trait set — the two mechanisms are independent,
    // so surviving a scene transition grants no save eligibility.
    const auto survivors = registry.view<world_persistence__Sparkle, std_core__Persistent>();
    REQUIRE(std::ranges::distance(survivors.begin(), survivors.end()) == 1);

    const auto snapshot = capture(registry);
    CHECK(count_for_archetype(snapshot, "world_persistence.Particle") == 0);
}

TEST_CASE("a hierarchical child with an ineligible parent is recorded as a root",
          "[runtime][persistence][capture]") {
    World world;
    const auto snapshot = capture(world.registry);

    // Formation declares no persist field anywhere in its own trait set, so it
    // is never captured even though its child is.
    CHECK(record_for_archetype(snapshot, "world_persistence.Formation") == nullptr);

    const auto* member = record_for_archetype(snapshot, "world_persistence.Formation/Member");
    REQUIRE(member != nullptr);
    CHECK_FALSE(member->parent.present);

    const auto* badge = trait_of(*member, "world_persistence.Badge");
    REQUIRE(badge != nullptr);
    CHECK(int_field(badge->persisted, "rank") == 1);
}

TEST_CASE("a world snapshot orders entities by relative creation order", "[runtime][persistence][capture]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 1, 0);
    drive_frame(registry, kStep);
    const auto first_enemy = *registry.view<world_persistence__Health, std_transform_flat__LocalTransform>().begin();
    registry.get<world_persistence__Health>(first_enemy).current = 111;

    queue_spawns(registry, 1, 0);
    drive_frame(registry, kStep);
    entt::entity second_enemy = entt::null;
    for (const auto entity : registry.view<world_persistence__Health, std_transform_flat__LocalTransform>()) {
        if (entity != first_enemy) {
            second_enemy = entity;
        }
    }
    REQUIRE(second_enemy != entt::entity{entt::null});
    registry.get<world_persistence__Health>(second_enemy).current = 222;

    const auto snapshot = capture(registry);

    // The boss is created at module load, before either runtime spawn.
    REQUIRE_FALSE(snapshot.entities.empty());
    CHECK(snapshot.entities.front().archetype == "world_persistence.Boss");

    std::vector<int> enemy_currents;
    for (const auto& record : snapshot.entities) {
        if (record.archetype == "world_persistence.Enemy") {
            const auto* health = trait_of(record, "world_persistence.Health");
            REQUIRE(health != nullptr);
            enemy_currents.push_back(int_field(health->persisted, "current"));
        }
    }
    REQUIRE(enemy_currents.size() == 2);
    // Spawn order, not document or memory order, decides this sequence.
    CHECK(enemy_currents[0] == 111);
    CHECK(enemy_currents[1] == 222);
}

TEST_CASE("capture leaves the live world unchanged", "[runtime][persistence][capture]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 1, 1);
    drive_frame(registry, kStep);

    const auto entity_count = registry.view<entt::entity>().size();
    const auto boss         = entity_with<world_persistence__Health>(registry);
    const auto before       = registry.get<world_persistence__Health>(boss);

    const auto snapshot = capture(registry);
    CHECK_FALSE(snapshot.entities.empty());
    CHECK(registry.view<entt::entity>().size() == entity_count);
    CHECK(registry.get<world_persistence__Health>(boss).current == before.current);
    CHECK(registry.get<world_persistence__Health>(boss).maximum == before.maximum);
}

TEST_CASE("capture runs no gameplay handler, even one a pending trigger would fire on the next frame",
          "[runtime][persistence][capture]") {
    World world;
    auto& registry = world.registry;

    // SpawnWorld's `on tick` would spawn an enemy and decrement this — but
    // only once a frame actually runs. Capture must not be that trigger.
    queue_spawns(registry, 3, 0);

    const auto entity_count_before = registry.view<entt::entity>().size();
    const auto snapshot            = capture(registry);
    CHECK_FALSE(snapshot.entities.empty());

    CHECK(registry.view<entt::entity>().size() == entity_count_before);
    const auto enemy_view = registry.view<world_persistence__Health, std_transform_flat__LocalTransform>();
    CHECK(enemy_view.begin() == enemy_view.end());
    auto& spawner = registry.get<world_persistence__Spawner>(entity_with<world_persistence__Spawner>(registry));
    CHECK(spawner.pending_enemies == 3);
}

TEST_CASE("a persistent list past the configured collection bound is truncated, not unbounded",
          "[runtime][persistence][capture][limits]") {
    World world;
    auto& registry = world.registry;

    constexpr auto kOverLimit = cactus::persistence::kMaxPersistenceCollectionSize + 5;
    auto& cargo               = registry.get<world_persistence__Cargo>(entity_with<world_persistence__Cargo>(registry));
    cargo.slots.assign(kOverLimit, 7);

    const auto snapshot   = capture(registry);
    const auto* warehouse = record_for_archetype(snapshot, "world_persistence.Warehouse");
    REQUIRE(warehouse != nullptr);
    const auto* cargo_trait = trait_of(*warehouse, "world_persistence.Cargo");
    REQUIRE(cargo_trait != nullptr);
    const auto* slots = cactus::persistence::find_field(cargo_trait->persisted, "slots");
    REQUIRE(slots != nullptr);

    CHECK(slots->value.as_list().items.size() == cactus::persistence::kMaxPersistenceCollectionSize);
    CHECK(snapshot.truncated);
}

TEST_CASE("a persistent list within the configured collection bound is not truncated",
          "[runtime][persistence][capture][limits]") {
    World world;
    auto& registry = world.registry;

    auto& cargo = registry.get<world_persistence__Cargo>(entity_with<world_persistence__Cargo>(registry));
    cargo.slots.assign(3, 7);

    const auto snapshot = capture(registry);
    CHECK_FALSE(snapshot.truncated);
}

TEST_CASE("capture preserves the exact 32-bit int range, not just typical gameplay values",
          "[runtime][persistence][capture][numeric]") {
    World world;
    auto& registry = world.registry;

    auto& health = registry.get<world_persistence__Health>(entity_with<world_persistence__Health>(registry));

    health.current      = std::numeric_limits<std::int32_t>::lowest();
    auto snapshot       = capture(registry);
    const auto* boss    = record_for_archetype(snapshot, "world_persistence.Boss");
    REQUIRE(boss != nullptr);
    CHECK(int_field(trait_of(*boss, "world_persistence.Health")->persisted, "current") ==
          std::numeric_limits<std::int32_t>::lowest());

    health.current = std::numeric_limits<std::int32_t>::max();
    snapshot        = capture(registry);
    boss            = record_for_archetype(snapshot, "world_persistence.Boss");
    REQUIRE(boss != nullptr);
    CHECK(int_field(trait_of(*boss, "world_persistence.Health")->persisted, "current") ==
          std::numeric_limits<std::int32_t>::max());
}

TEST_CASE("capture preserves the exact backend float bit pattern, not a rounded approximation",
          "[runtime][persistence][capture][numeric]") {
    World world;
    auto& registry = world.registry;

    // Not exactly representable in a handful of decimal digits: a lossy
    // capture path would round this rather than reproduce it exactly.
    constexpr float kAwkward = 1.0F / 3.0F;
    auto& gauge = registry.get<world_persistence__Precision>(entity_with<world_persistence__Precision>(registry));
    gauge.value = kAwkward;

    const auto snapshot = capture(registry);
    const auto* record  = record_for_archetype(snapshot, "world_persistence.Gauge");
    REQUIRE(record != nullptr);
    const auto* precision = trait_of(*record, "world_persistence.Precision");
    REQUIRE(precision != nullptr);
    CHECK(float_field(precision->persisted, "value") == kAwkward);
}

TEST_CASE("a real captured snapshot survives write-then-read through the in-memory adapter as an equal document",
          "[runtime][persistence][adapter]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 1, 0);
    drive_frame(registry, kStep);

    const auto original = capture(registry);
    REQUIRE_FALSE(original.entities.empty());

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(memory_adapter.as_adapter());

    const auto write_outcome = cactus::runtime::entt_backend::generated_execute_save_request(
        "slot1", 1, cactus::runtime::entt_backend::generated_schema, original);
    REQUIRE(write_outcome.ok);

    const auto read_result = cactus::runtime::entt_backend::read_persistence_document(
        "slot1", cactus::runtime::entt_backend::generated_schema);
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == original);
}

TEST_CASE("the same real captured snapshot round-trips to equal documents through two different adapter formats",
          "[runtime][persistence][adapter]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 1, 0);
    drive_frame(registry, kStep);

    const auto original = capture(registry);
    REQUIRE_FALSE(original.entities.empty());
    const auto& schema = cactus::runtime::entt_backend::generated_schema;

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    const auto memory_write = memory_adapter.as_adapter().write("slot1", schema, original);
    REQUIRE(memory_write.ok);
    const auto memory_read = memory_adapter.as_adapter().read("slot1", schema);
    REQUIRE(memory_read.ok);

    cactus_test::ScopedTempDirectory dir;
    const auto file_adapter = cactus::runtime::entt_backend::make_example_file_adapter(dir.path());
    const auto file_write   = file_adapter.write("slot1", schema, original);
    REQUIRE(file_write.ok);
    const auto file_read = file_adapter.read("slot1", schema);
    REQUIRE(file_read.ok);

    // Format independence (design decision 7): two adapters encoding the same
    // generated snapshot differently still decode back to equal documents,
    // neither only accidentally matching the original.
    CHECK(memory_read.snapshot == original);
    CHECK(file_read.snapshot == original);
    CHECK(memory_read.snapshot == file_read.snapshot);
}

TEST_CASE("restoring the same document read back through either adapter produces equivalent worlds",
          "[runtime][persistence][restore][adapter]") {
    World world;
    auto& registry = world.registry;

    auto& boss_health   = registry.get<world_persistence__Health>(entity_with<world_persistence__Health>(registry));
    boss_health.current = 321;
    const auto original = capture(registry);
    const auto& schema  = cactus::runtime::entt_backend::generated_schema;

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    REQUIRE(memory_adapter.as_adapter().write("slot1", schema, original).ok);
    const auto memory_read = memory_adapter.as_adapter().read("slot1", schema);
    REQUIRE(memory_read.ok);

    cactus_test::ScopedTempDirectory dir;
    const auto file_adapter = cactus::runtime::entt_backend::make_example_file_adapter(dir.path());
    REQUIRE(file_adapter.write("slot1", schema, original).ok);
    const auto file_read = file_adapter.read("slot1", schema);
    REQUIRE(file_read.ok);

    entt::registry memory_world;
    init_and_load(memory_world);
    const auto memory_outcome =
        cactus::runtime::entt_backend::generated_restore_world(memory_world, memory_read.snapshot);
    REQUIRE(memory_outcome.ok);

    entt::registry file_world;
    init_and_load(file_world);
    const auto file_outcome = cactus::runtime::entt_backend::generated_restore_world(file_world, file_read.snapshot);
    REQUIRE(file_outcome.ok);

    auto& memory_boss = memory_world.get<world_persistence__Health>(entity_with<world_persistence__Health>(memory_world));
    auto& file_boss    = file_world.get<world_persistence__Health>(entity_with<world_persistence__Health>(file_world));
    CHECK(memory_boss.maximum == file_boss.maximum);
    CHECK(memory_boss.current == file_boss.current);
    CHECK(file_boss.current == 321);
}

// ── World restore (add-world-snapshot-restore) ──────────────────────────────

TEST_CASE("restoring a captured snapshot reproduces the boss archetype baseline and its persisted value",
          "[runtime][persistence][restore]") {
    World world;
    auto& registry = world.registry;

    auto& boss_health    = registry.get<world_persistence__Health>(entity_with<world_persistence__Health>(registry));
    boss_health.current  = 321;
    const auto snapshot  = capture(registry);

    // Mutate the live world so a correct restore has something real to undo.
    boss_health.current = 999;
    boss_health.maximum = 1;

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    auto& restored_health = registry.get<world_persistence__Health>(entity_with<world_persistence__Health>(registry));
    CHECK(restored_health.maximum == 500);
    CHECK(restored_health.current == 321);
}

TEST_CASE("restoring an enemy reproduces its original spawn argument, not a later mutation",
          "[runtime][persistence][restore]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 1, 0);
    drive_frame(registry, kStep);
    const auto enemy = *registry.view<world_persistence__Health, std_transform_flat__LocalTransform>().begin();
    REQUIRE(registry.get<world_persistence__Health>(enemy).maximum == 700);

    const auto snapshot = capture(registry);

    // Mutate the unmarked field after capture: a correct restore must not
    // re-evaluate the spawn expression or reflect this mutation.
    registry.get<world_persistence__Health>(enemy).maximum = 12345;

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto restored_enemy =
        *registry.view<world_persistence__Health, std_transform_flat__LocalTransform>().begin();
    CHECK(registry.get<world_persistence__Health>(restored_enemy).maximum == 700);
}

TEST_CASE("restoring a hierarchical child with no captured parent restores it as a root",
          "[runtime][persistence][restore]") {
    World world;
    auto& registry      = world.registry;
    const auto snapshot = capture(registry);

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto view = registry.view<world_persistence__Badge>();
    REQUIRE(std::ranges::distance(view.begin(), view.end()) == 1);
    const auto member = *view.begin();
    CHECK(registry.get<world_persistence__Badge>(member).rank == 1);
    CHECK_FALSE(registry.all_of<std_core__Parent>(member));
}

TEST_CASE("a document captured by the existing save path restores to an equivalent world through the "
          "in-memory adapter",
          "[runtime][persistence][restore][adapter]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 1, 1);
    drive_frame(registry, kStep);

    const auto original = capture(registry);
    REQUIRE_FALSE(original.entities.empty());

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(memory_adapter.as_adapter());
    const auto write_outcome = cactus::runtime::entt_backend::generated_execute_save_request(
        "slot1", 1, cactus::runtime::entt_backend::generated_schema, original);
    REQUIRE(write_outcome.ok);

    const auto read_result = cactus::runtime::entt_backend::read_persistence_document(
        "slot1", cactus::runtime::entt_backend::generated_schema);
    REQUIRE(read_result.ok);

    const auto restore_outcome =
        cactus::runtime::entt_backend::generated_restore_world(registry, read_result.snapshot);
    REQUIRE(restore_outcome.ok);

    // A faithful restore recaptures to a snapshot equal to the original, up to
    // one acknowledged asymmetry: a record whose *original* live parent was
    // itself ineligible (Formation/Member here) captured a non-zero absent
    // EntityRef id, taken from that specific ineligible entity's identity.
    // Formation never exists in the restored world at all, so the restored
    // root's absent parent has no entity to take an id from and recaptures as
    // EntityRef{} (id 0). Both are "no parent, don't care about id" — the id
    // of an absent reference was never meant to be compared — so entity-level
    // equality here ignores id specifically where present is false.
    const auto recaptured = capture(registry);
    REQUIRE(recaptured.entities.size() == original.entities.size());
    for (std::size_t i = 0; i < original.entities.size(); ++i) {
        INFO("entity index " << i << " archetype " << original.entities[i].archetype);
        CHECK(recaptured.entities[i].archetype == original.entities[i].archetype);
        CHECK(recaptured.entities[i].id == original.entities[i].id);
        CHECK(recaptured.entities[i].parent.present == original.entities[i].parent.present);
        if (original.entities[i].parent.present) {
            CHECK(recaptured.entities[i].parent.id == original.entities[i].parent.id);
        }
        CHECK(recaptured.entities[i].traits == original.entities[i].traits);
    }
}

TEST_CASE("a successful restore drops the ineligible camera and RestoreCompleted lets gameplay rebuild it",
          "[runtime][persistence][restore][headless]") {
    World world;
    auto& registry = world.registry;

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(memory_adapter.as_adapter());

    const auto snapshot       = capture(registry);
    const auto write_outcome  = cactus::runtime::entt_backend::generated_execute_save_request(
        "world_persistence", 0, cactus::runtime::entt_backend::generated_schema, snapshot);
    REQUIRE(write_outcome.ok);

    REQUIRE(registry.view<world_persistence__ActiveCamera>().size() == 1);
    const auto original_camera = *registry.view<world_persistence__ActiveCamera>().begin();
    // A distinguishing, non-default signal on the pre-restore camera: a
    // rebuilt camera starts from ActiveCamera's declared default (1.0)
    // again, so this proves a fresh entity rather than the old one
    // surviving untouched. Comparing entity ids would not: publication
    // resets the deferred-spawn reservation cursor (design decision 2), so
    // the rebuilt camera can legitimately land on the same numeric id as
    // the one it replaced.
    registry.get<world_persistence__ActiveCamera>(original_camera).zoom = 5.0F;

    auto& control = registry.get<world_persistence__RestoreControl>(
        entity_with<world_persistence__RestoreControl>(registry));
    control.pending_restore = 1;

    drive_frame(registry, kStep);

    // The camera has no persist field, so a full replacement drops it; only
    // RebuildCameraAfterRestore's `on storage.RestoreCompleted:` handler
    // creates the one that exists afterwards.
    REQUIRE(registry.view<world_persistence__ActiveCamera>().size() == 1);
    const auto restored_camera = *registry.view<world_persistence__ActiveCamera>().begin();
    CHECK(registry.get<world_persistence__ActiveCamera>(restored_camera).zoom == 1.0F);
}

TEST_CASE("restoring an empty compatible document produces an empty world",
          "[runtime][persistence][restore][publish]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 1, 1);
    drive_frame(registry, kStep);
    REQUIRE(registry.view<entt::entity>().size() > 0);

    cactus::persistence::Snapshot empty;
    empty.schema_revision    = cactus::runtime::entt_backend::generated_schema.revision;
    empty.schema_fingerprint = cactus::runtime::entt_backend::generated_schema.fingerprint;
    empty.module             = "world_persistence";

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, empty);
    REQUIRE(outcome.ok);
    CHECK(registry.view<entt::entity>().size() == 0);
}

TEST_CASE("entities carrying std.core.Persistent do not survive restore",
          "[runtime][persistence][restore][publish]") {
    World world;
    auto& registry = world.registry;

    queue_spawns(registry, 0, 1);
    drive_frame(registry, kStep);
    const auto survivors = registry.view<world_persistence__Sparkle, std_core__Persistent>();
    REQUIRE(std::ranges::distance(survivors.begin(), survivors.end()) == 1);

    const auto snapshot = capture(registry);
    // The particle carries Persistent but no persist field anywhere in its
    // trait set, so it is absent from the document — capture already proves
    // this elsewhere. Restore replacing the live world is the new claim
    // here: a full replacement is not scene-transition survival, so the
    // live particle must not survive it either.
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);
    CHECK(registry.view<std_core__Persistent>().size() == 0);
}

TEST_CASE("a successful restore discards old deferred events and fixed-step catch-up backlog",
          "[runtime][persistence][restore][cancellation]") {
    World world;
    auto& registry = world.registry;

    const auto snapshot = capture(registry);

    auto& scheduler = cactus::runtime::entt_backend::generated_scheduler_state();
    scheduler.activation.deferred_events.push_back(
        cactus::runtime::entt_backend::QueuedEvent<cactus::runtime::entt_backend::EventOccurrence>{.occurrence = std_core__destroyEvent{}});
    scheduler.std_core__fixed_tick.accumulator = 10.0;
    REQUIRE_FALSE(scheduler.activation.deferred_events.empty());

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    CHECK(scheduler.activation.deferred_events.empty());
    CHECK(scheduler.std_core__fixed_tick.accumulator == 0.0);
}

TEST_CASE("a failed restore preserves old-world scheduler work untouched",
          "[runtime][persistence][restore][cancellation]") {
    World world;
    auto& registry = world.registry;

    cactus::persistence::Snapshot incompatible;
    incompatible.schema_fingerprint = cactus::runtime::entt_backend::generated_schema.fingerprint + 1;

    auto& scheduler = cactus::runtime::entt_backend::generated_scheduler_state();
    scheduler.activation.deferred_events.push_back(
        cactus::runtime::entt_backend::QueuedEvent<cactus::runtime::entt_backend::EventOccurrence>{.occurrence = std_core__destroyEvent{}});
    scheduler.std_core__fixed_tick.accumulator = 10.0;

    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(registry, incompatible);
    REQUIRE_FALSE(outcome.ok);

    CHECK(scheduler.activation.deferred_events.size() == 1);
    CHECK(scheduler.std_core__fixed_tick.accumulator == 10.0);
}

TEST_CASE("a save and a restore accepted at the same boundary are both processed",
          "[runtime][persistence][restore][scheduling]") {
    World world;
    auto& registry = world.registry;

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(memory_adapter.as_adapter());

    const auto snapshot = capture(registry);
    REQUIRE(cactus::runtime::entt_backend::generated_execute_save_request(
                "world_persistence", 0, cactus::runtime::entt_backend::generated_schema, snapshot)
                .ok);

    // Queued directly in emission order, as generated code would from two
    // `emit storage.SaveRequested:`/`emit storage.RestoreRequested:`
    // statements in the same activation.
    cactus::runtime::entt_backend::generated_queue_save_request("save_check", 1);
    cactus::runtime::entt_backend::generated_queue_restore_request("world_persistence", 2);

    cactus::runtime::entt_backend::generated_process_persistence_boundary(registry);

    const auto save_read = cactus::runtime::entt_backend::read_persistence_document(
        "save_check", cactus::runtime::entt_backend::generated_schema);
    CHECK(save_read.ok);
}

TEST_CASE("a save queued after a restore in the same batch does not reuse a save cached before the restore",
          "[runtime][persistence][restore][scheduling]") {
    World world;
    auto& registry = world.registry;

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(memory_adapter.as_adapter());

    registry.get<world_persistence__Health>(entity_with<world_persistence__Health>(registry)).current = 111;
    REQUIRE(cactus::runtime::entt_backend::generated_execute_save_request(
                "restore_target", 0, cactus::runtime::entt_backend::generated_schema, capture(registry))
                .ok);

    registry.get<world_persistence__Health>(entity_with<world_persistence__Health>(registry)).current = 999;

    // A boundary's persistence-request loop caches a save's snapshot across
    // consecutive saves (so N saves with no restore between them capture
    // once, not N times), invalidated only by a successful restore. The
    // first save here (current == 999) must populate that cache; the
    // restore must discard it; the second save must then observe the
    // restored world (current == 111), not the cached pre-restore one.
    cactus::runtime::entt_backend::generated_queue_save_request("cache_probe", 1);
    cactus::runtime::entt_backend::generated_queue_restore_request("restore_target", 2);
    cactus::runtime::entt_backend::generated_queue_save_request("after_restore_check", 3);

    cactus::runtime::entt_backend::generated_process_persistence_boundary(registry);

    const auto save_read = cactus::runtime::entt_backend::read_persistence_document(
        "after_restore_check", cactus::runtime::entt_backend::generated_schema);
    REQUIRE(save_read.ok);

    const auto* boss = record_for_archetype(save_read.snapshot, "world_persistence.Boss");
    REQUIRE(boss != nullptr);
    const auto* health = trait_of(*boss, "world_persistence.Health");
    REQUIRE(health != nullptr);
    CHECK(int_field(health->persisted, "current") == 111);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
