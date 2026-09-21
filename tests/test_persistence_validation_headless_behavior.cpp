// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "common/persistence_validation.hpp"
#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

namespace {

using cactus_headless_test::init_and_load;
using cactus::persistence::find_field;
using cactus::persistence::find_trait;
using cactus::persistence::Snapshot;
using cactus::persistence::TraitRecord;
using cactus::persistence::Value;
using cactus::persistence::ValidationError;
using cactus::persistence::validate_document;

const auto& schema = cactus::runtime::entt_backend::generated_schema;

struct World {
    entt::registry registry;

    World() { init_and_load(registry); }
};

Snapshot valid_snapshot() {
    World world;
    return cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);
}

bool any_error_at(const std::vector<ValidationError>& errors, const std::string& path) {
    return std::ranges::any_of(errors, [&](const ValidationError& error) { return error.field_path == path; });
}

// Mutable, unlike cactus::persistence::find_trait: several tests below
// corrupt a field on the returned record afterward.
TraitRecord& require_trait(std::vector<TraitRecord>& traits, std::string_view trait_name) {
    auto found =
        std::ranges::find_if(traits, [&](const TraitRecord& trait) { return trait.trait == trait_name; });
    REQUIRE(found != traits.end());
    return *found;
}

}  // namespace

TEST_CASE("a validly captured document has no validation errors", "[persistence][validation]") {
    const auto snapshot = valid_snapshot();
    const auto errors   = validate_document(schema, snapshot);
    CHECK(errors.empty());
}

TEST_CASE("an incompatible schema descriptor fails before any other check runs",
          "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    snapshot.schema_fingerprint += 1;

    const auto errors = validate_document(schema, snapshot);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].field_path == "$schema");
}

TEST_CASE("a duplicate document identity is reported", "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    REQUIRE(snapshot.entities.size() >= 2);
    snapshot.entities[1].id = snapshot.entities[0].id;

    const auto errors = validate_document(schema, snapshot);
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) { return error.message == "duplicate document identity"; }));
}

TEST_CASE("an unknown archetype is reported", "[persistence][validation]") {
    auto snapshot                    = valid_snapshot();
    snapshot.entities[0].archetype = "persistence_references_runtime.NoSuchArchetype";

    const auto errors = validate_document(schema, snapshot);
    const auto path    = "persistence_references_runtime.NoSuchArchetype[" + std::to_string(snapshot.entities[0].id) + "]";
    REQUIRE(any_error_at(errors, path));
}

TEST_CASE("an unknown trait is reported", "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    snapshot.entities[0].traits.push_back(TraitRecord{.trait = "persistence_references_runtime.NoSuchTrait"});

    const auto errors = validate_document(schema, snapshot);
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) { return error.message == "unknown trait"; }));
}

TEST_CASE("a field of the wrong value kind is reported", "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    auto& rival    = require_trait(snapshot.entities[0].traits, "persistence_references_runtime.Rival");
    for (auto& field : rival.persisted) {
        if (field.name == "rival") {
            field.value = Value::of_string("not an entity reference");
        }
    }

    const auto errors = validate_document(schema, snapshot);
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) { return error.message == "wrong value kind"; }));
}

TEST_CASE("a missing required field is reported", "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    auto& rival    = require_trait(snapshot.entities[0].traits, "persistence_references_runtime.Rival");
    rival.persisted.clear();

    const auto errors = validate_document(schema, snapshot);
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) { return error.message == "missing required field"; }));
}

TEST_CASE("a dangling reference to a non-existent document identity is reported",
          "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    auto& rival    = require_trait(snapshot.entities[0].traits, "persistence_references_runtime.Rival");
    for (auto& field : rival.persisted) {
        if (field.name == "rival") {
            field.value = Value::of_entity(cactus::persistence::EntityRef{.id = 99999, .present = true});
        }
    }

    const auto errors = validate_document(schema, snapshot);
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) {
        return error.message == "reference names a document identity with no matching record";
    }));
}

TEST_CASE("a hierarchy cycle among parent links is reported", "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    REQUIRE(snapshot.entities.size() >= 2);
    snapshot.entities[0].parent = cactus::persistence::EntityRef{.id = snapshot.entities[1].id, .present = true};
    snapshot.entities[1].parent = cactus::persistence::EntityRef{.id = snapshot.entities[0].id, .present = true};

    const auto errors = validate_document(schema, snapshot);
    CHECK(std::ranges::any_of(
        errors, [](const ValidationError& error) { return error.message == "parent chain contains a cycle"; }));
}

TEST_CASE("a list past the configured collection size limit is reported", "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    auto& roster   = require_trait(snapshot.entities[0].traits, "persistence_references_runtime.Roster");
    for (auto& field : roster.persisted) {
        if (field.name == "members") {
            cactus::persistence::ListValue oversized;
            oversized.items.assign(cactus::persistence::kMaxPersistenceCollectionSize + 1,
                                   Value::of_entity(cactus::persistence::EntityRef{}));
            field.value = Value::of_list(std::move(oversized));
        }
    }

    const auto errors = validate_document(schema, snapshot);
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) {
        return error.message == "list exceeds the configured collection size limit";
    }));
}

TEST_CASE("multiple problems are all reported together with field paths, not just the first",
          "[persistence][validation]") {
    auto snapshot = valid_snapshot();
    REQUIRE(snapshot.entities.size() >= 2);
    snapshot.entities[0].archetype = "persistence_references_runtime.NoSuchArchetype";
    snapshot.entities[1].traits.push_back(TraitRecord{.trait = "persistence_references_runtime.NoSuchTrait"});

    const auto errors = validate_document(schema, snapshot);
    CHECK(errors.size() >= 2);
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) { return error.message == "unknown archetype"; }));
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) { return error.message == "unknown trait"; }));
    for (const auto& error : errors) {
        CHECK_FALSE(error.field_path.empty());
    }
}

TEST_CASE("an adapter-approved but invalid document is still rejected by runtime validation",
          "[persistence][validation][restore]") {
    World world;
    auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);
    REQUIRE(snapshot.entities.size() >= 2);
    // An adapter's own encoding might not notice this, but runtime
    // validation must — no adapter can be trusted to enforce it.
    snapshot.entities[1].id = snapshot.entities[0].id;

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(memory_adapter.as_adapter());
    const auto write_outcome =
        cactus::runtime::entt_backend::generated_execute_save_request("slot1", 1, schema, snapshot);
    REQUIRE(write_outcome.ok);

    const auto read_result = cactus::runtime::entt_backend::read_persistence_document("slot1", schema);
    REQUIRE(read_result.ok);  // the adapter's own encoding considers this document valid

    const auto entities_before = world.registry.view<entt::entity>().size();
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, read_result.snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "invalid_data");
    CHECK(world.registry.view<entt::entity>().size() == entities_before);
}

TEST_CASE("a document with only value-kind problems is rejected with the unsupported_value code",
          "[persistence][validation][restore]") {
    World world;
    auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);
    auto& rival    = require_trait(snapshot.entities[0].traits, "persistence_references_runtime.Rival");
    for (auto& field : rival.persisted) {
        if (field.name == "rival") {
            field.value = Value::of_string("not an entity reference");
        }
    }

    const auto entities_before = world.registry.view<entt::entity>().size();
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "unsupported_value");
    CHECK(world.registry.view<entt::entity>().size() == entities_before);
}

TEST_CASE("a resource that no longer exists in this build fails staging and leaves the old world intact",
          "[persistence][validation][restore]") {
    World world;
    auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);
    auto& badge    = require_trait(snapshot.entities[0].traits, "persistence_references_runtime.Badge");
    for (auto& field : badge.persisted) {
        if (field.name == "icon") {
            field.value = Value::of_asset("persistence_references_runtime.NoSuchIcon");
        }
    }

    const auto entities_before = world.registry.view<entt::entity>().size();
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "resource_preparation_failure");
    CHECK(world.registry.view<entt::entity>().size() == entities_before);
}

TEST_CASE("an incompatible schema descriptor fails before any world mutation",
          "[persistence][validation][restore]") {
    World world;
    auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);
    snapshot.schema_fingerprint += 1;

    const auto entities_before = world.registry.view<entt::entity>().size();
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "incompatible_schema");
    CHECK(world.registry.view<entt::entity>().size() == entities_before);
}

TEST_CASE("a restore request with no registered adapter fails as adapter_unavailable",
          "[persistence][validation][restore][request]") {
    World world;

    const auto outcome = cactus::runtime::entt_backend::generated_execute_restore_request(
        world.registry, "slot1", 42, schema);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.slot == "slot1");
    CHECK(outcome.request_id == 42);
    CHECK(outcome.code == "adapter_unavailable");
}

TEST_CASE("a restore request for a slot the adapter has never written fails as io_failure",
          "[persistence][validation][restore][request]") {
    World world;

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(memory_adapter.as_adapter());

    const auto outcome = cactus::runtime::entt_backend::generated_execute_restore_request(
        world.registry, "never_saved", 7, schema);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.slot == "never_saved");
    CHECK(outcome.request_id == 7);
    CHECK(outcome.code == "io_failure");
}

TEST_CASE("a successful restore request round-trips through the adapter and reports the correlated outcome",
          "[persistence][validation][restore][request]") {
    World world;
    const auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);

    cactus_test::InMemoryPersistenceAdapter memory_adapter;
    cactus_test::ScopedPersistenceAdapter scoped_adapter(memory_adapter.as_adapter());
    const auto write_outcome =
        cactus::runtime::entt_backend::generated_execute_save_request("slot1", 1, schema, snapshot);
    REQUIRE(write_outcome.ok);

    const auto outcome = cactus::runtime::entt_backend::generated_execute_restore_request(
        world.registry, "slot1", 9, schema);
    CHECK(outcome.ok);
    CHECK(outcome.slot == "slot1");
    CHECK(outcome.request_id == 9);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
