// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "common/persistence_validation.hpp"
#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_snapshot_edit.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

namespace {

using cactus::persistence::Snapshot;
using cactus::persistence::validate_document;
using cactus::persistence::ValidationError;
using cactus::persistence::Value;
using cactus_headless_test::init_and_load;
using cactus_test::EditableRecord;
using cactus_test::EditableTrait;
using cactus_test::edited;

const auto& schema = cactus::runtime::entt_backend::generated_schema;

constexpr std::string_view kRival = "persistence_references_runtime.Rival";

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

bool any_message(const std::vector<ValidationError>& errors, std::string_view message) {
    return std::ranges::any_of(errors, [&](const ValidationError& error) { return error.message == message; });
}

EditableTrait& require_trait(EditableRecord& record, std::string_view trait_name) {
    auto* found = record.trait(trait_name);
    REQUIRE(found != nullptr);
    return *found;
}

Value& require_field(EditableTrait& trait, std::string_view field_name) {
    auto* found = trait.field(field_name);
    REQUIRE(found != nullptr);
    return *found;
}

std::string record_path(const Snapshot& snapshot, std::size_t index) {
    const auto entity = snapshot.entity(index);
    return std::string(entity.archetype()) + "[" + std::to_string(entity.id()) + "]";
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
    snapshot.set_schema(snapshot.schema_revision(), snapshot.schema_fingerprint() + 1);

    const auto errors = validate_document(schema, snapshot);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].field_path == "$schema");
}

TEST_CASE("a duplicate document identity is reported", "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        REQUIRE(records.size() >= 2);
        records[1].id = records[0].id;
    });

    const auto errors = validate_document(schema, snapshot);
    CHECK(any_message(errors, "duplicate document identity"));
}

TEST_CASE("an unknown archetype is reported", "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        records[0].archetype = "persistence_references_runtime.NoSuchArchetype";
    });

    const auto errors = validate_document(schema, snapshot);
    const auto path =
        "persistence_references_runtime.NoSuchArchetype[" + std::to_string(snapshot.entity(0).id()) + "]";
    REQUIRE(any_error_at(errors, path));
}

TEST_CASE("an unknown trait is reported", "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        records[0].traits.push_back(EditableTrait{.name = "persistence_references_runtime.NoSuchTrait"});
    });

    const auto errors = validate_document(schema, snapshot);
    CHECK(any_message(errors, "unknown trait"));
}

TEST_CASE("a field of the wrong value kind is reported", "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        require_field(require_trait(records[0], kRival), "rival") = Value::of_string("not an entity reference");
    });

    const auto errors = validate_document(schema, snapshot);
    const auto path   = record_path(snapshot, 0) + "." + std::string(kRival) + ".rival";
    CHECK(std::ranges::any_of(errors, [&](const ValidationError& error) {
        return error.message == "wrong value kind" && error.field_path == path;
    }));
}

TEST_CASE("a missing required field is reported", "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        require_trait(records[0], kRival).persisted.clear();
    });

    const auto errors = validate_document(schema, snapshot);
    CHECK(any_message(errors, "missing required field"));
}

TEST_CASE("a dangling reference to a non-existent document identity is reported",
          "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        require_field(require_trait(records[0], kRival), "rival") =
            Value::of_entity(cactus::persistence::EntityRef{.id = 99999, .present = true});
    });

    const auto errors = validate_document(schema, snapshot);
    CHECK(any_message(errors, "reference names a document identity with no matching record"));
}

TEST_CASE("a hierarchy cycle among parent links is reported", "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        REQUIRE(records.size() >= 2);
        records[0].parent = cactus::persistence::EntityRef{.id = records[1].id, .present = true};
        records[1].parent = cactus::persistence::EntityRef{.id = records[0].id, .present = true};
    });

    const auto errors = validate_document(schema, snapshot);
    CHECK(any_message(errors, "parent chain contains a cycle"));
}

TEST_CASE("a list past the configured collection size limit is reported", "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        cactus::persistence::ListValue oversized;
        oversized.items.assign(cactus::persistence::kMaxPersistenceCollectionSize + 1,
                               Value::of_entity(cactus::persistence::EntityRef{}));
        require_field(require_trait(records[0], "persistence_references_runtime.Roster"), "members") =
            Value::of_list(std::move(oversized));
    });

    const auto errors = validate_document(schema, snapshot);
    CHECK(any_message(errors, "list exceeds the configured collection size limit"));
}

TEST_CASE("a document with more records than the list limit is not rejected for its size",
          "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        REQUIRE_FALSE(records.empty());
        auto next_id    = std::ranges::max(records, {}, &EditableRecord::id).id;
        const auto seed = records.back();
        while (records.size() <= cactus::persistence::kMaxPersistenceCollectionSize + 1000) {
            auto copy   = seed;
            copy.id     = ++next_id;
            copy.parent = cactus::persistence::EntityRef{};
            records.push_back(std::move(copy));
        }
    });

    const auto errors = validate_document(schema, snapshot);
    CHECK_FALSE(any_message(errors, "entity count exceeds the configured limit"));
}

TEST_CASE("multiple problems are all reported together with field paths, not just the first",
          "[persistence][validation]") {
    const auto snapshot = edited(valid_snapshot(), [](std::vector<EditableRecord>& records) {
        REQUIRE(records.size() >= 2);
        records[0].archetype = "persistence_references_runtime.NoSuchArchetype";
        records[1].traits.push_back(EditableTrait{.name = "persistence_references_runtime.NoSuchTrait"});
    });

    const auto errors = validate_document(schema, snapshot);
    CHECK(errors.size() >= 2);
    CHECK(any_message(errors, "unknown archetype"));
    CHECK(any_message(errors, "unknown trait"));
    for (const auto& error : errors) {
        CHECK_FALSE(error.field_path.empty());
    }
}

TEST_CASE("a truncated value payload is reported instead of being read past its end",
          "[persistence][validation]") {
    const auto captured = valid_snapshot();
    const auto rival    = captured.entity(0).trait(kRival);
    REQUIRE(rival.has_value());

    auto builder = cactus::persistence::SnapshotBuilder::with_tables_of(captured);
    builder.begin_entity(captured.entity(0).id(), builder.name(captured.entity(0).archetype()),
                         captured.entity(0).parent());
    builder.begin_trait(rival->shape());
    builder.begin_persisted();
    builder.put_bool(true);  // a reference takes five bytes
    builder.end_trait();
    const auto snapshot = std::move(builder).finish();

    const auto errors = validate_document(schema, snapshot);
    CHECK(std::ranges::any_of(errors, [&](const ValidationError& error) {
        return error.message == "value data is truncated or malformed" &&
               error.field_path == record_path(snapshot, 0) + "." + std::string(kRival);
    }));
}

TEST_CASE("an adapter-approved but invalid document is still rejected by runtime validation",
          "[persistence][validation][restore]") {
    World world;
    // An adapter's own encoding might not notice this, but runtime
    // validation must — no adapter can be trusted to enforce it.
    const auto snapshot = edited(cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry),
                                 [](std::vector<EditableRecord>& records) {
                                     REQUIRE(records.size() >= 2);
                                     records[1].id = records[0].id;
                                 });

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
    const auto snapshot = edited(cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry),
                                 [](std::vector<EditableRecord>& records) {
                                     require_field(require_trait(records[0], kRival), "rival") =
                                         Value::of_string("not an entity reference");
                                 });

    const auto entities_before = world.registry.view<entt::entity>().size();
    const auto outcome = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "unsupported_value");
    CHECK(world.registry.view<entt::entity>().size() == entities_before);
}

TEST_CASE("a resource that no longer exists in this build fails staging and leaves the old world intact",
          "[persistence][validation][restore]") {
    World world;
    const auto snapshot = edited(cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry),
                                 [](std::vector<EditableRecord>& records) {
                                     require_field(require_trait(records[0], "persistence_references_runtime.Badge"),
                                                   "icon") = Value::of_asset("persistence_references_runtime.NoSuchIcon");
                                 });

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
    snapshot.set_schema(snapshot.schema_revision(), snapshot.schema_fingerprint() + 1);

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

TEST_CASE("a record whose trait set no entity of its archetype can reach is rejected",
          "[persistence][validation][restore][archetype-reachability]") {
    World world;
    const auto captured = cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry);
    REQUIRE(validate_document(schema, captured).empty());
    cactus::persistence::DocumentId warrior_id = 0;
    const auto snapshot = edited(captured, [&](std::vector<EditableRecord>& records) {
        const auto warrior =
            std::ranges::find(records, "persistence_references_runtime.Warrior", &EditableRecord::archetype);
        REQUIRE(warrior != records.end());
        // Only a Prop ever carries Cosmetic.
        warrior->traits.push_back(EditableTrait{.name = "persistence_references_runtime.Cosmetic"});
        warrior_id = warrior->id;
    });

    const auto errors  = validate_document(schema, snapshot);
    const auto path    = "persistence_references_runtime.Warrior[" + std::to_string(warrior_id) + "]";
    const auto illegal = std::ranges::find_if(
        errors, [](const ValidationError& error) { return error.message.starts_with("illegal trait combination"); });
    REQUIRE(illegal != errors.end());
    CHECK(illegal->field_path == path);

    const auto entities_before = world.registry.view<entt::entity>().size();
    const auto outcome         = cactus::runtime::entt_backend::generated_restore_world(world.registry, snapshot);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "invalid_data");
    CHECK(world.registry.view<entt::entity>().size() == entities_before);
}

TEST_CASE("a record with a trait set its archetype can reach passes", "[persistence][validation][archetype-reachability]") {
    World world;
    const auto snapshot = edited(cactus::runtime::entt_backend::generated_capture_world_snapshot(world.registry),
                                 [](std::vector<EditableRecord>& records) {
                                     const auto warrior = std::ranges::find_if(records, [](EditableRecord& record) {
                                         return record.archetype == "persistence_references_runtime.Warrior" &&
                                                record.trait("persistence_references_runtime.Badge") == nullptr;
                                     });
                                     REQUIRE(warrior != records.end());
                                     // Any Warrior can gain a Badge.
                                     warrior->traits.push_back(
                                         EditableTrait{.name = "persistence_references_runtime.Badge"});
                                 });
    CHECK(std::ranges::none_of(validate_document(schema, snapshot), [](const ValidationError& error) {
        return error.message.starts_with("illegal trait combination");
    }));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
