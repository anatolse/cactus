// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cactus::runtime::entt_backend;
namespace persistence = cactus::persistence;

namespace {

// generated_persistence_state() is a function-local static: reset it between
// test cases so one test's registered adapter cannot leak into the next.
struct ResetPersistenceState {
    ResetPersistenceState() { generated_persistence_state() = PersistenceRuntimeState{}; }
    ~ResetPersistenceState() { generated_persistence_state() = PersistenceRuntimeState{}; }
    ResetPersistenceState(const ResetPersistenceState&)            = delete;
    ResetPersistenceState& operator=(const ResetPersistenceState&) = delete;
    ResetPersistenceState(ResetPersistenceState&&)                 = delete;
    ResetPersistenceState& operator=(ResetPersistenceState&&)      = delete;
};

persistence::SchemaDescriptor sample_schema() {
    return persistence::SchemaDescriptor{.revision = cactus::kPersistenceSchemaRevision, .fingerprint = 42ULL};
}

persistence::Snapshot sample_snapshot() {
    persistence::Snapshot snapshot;
    snapshot.schema_revision    = cactus::kPersistenceSchemaRevision;
    snapshot.schema_fingerprint = 42ULL;
    snapshot.module             = "world";
    persistence::EntityRecord record;
    record.id        = 1;
    record.archetype = "world.Boss";
    record.traits.push_back(persistence::TraitRecord{
        .trait = "world.Health", .persisted = {persistence::FieldValue{.name = "current", .value = persistence::Value::of_int(137)}}});
    snapshot.entities.push_back(std::move(record));
    return snapshot;
}

}  // namespace

TEST_CASE("read with no registered adapter fails as adapter_unavailable", "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    const auto result = read_persistence_document("slot1", sample_schema());
    CHECK_FALSE(result.ok);
    CHECK(result.code == "adapter_unavailable");
}

TEST_CASE("a write failure reported by the adapter is reflected in the outcome", "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    register_persistence_adapter(PersistenceAdapter{
        .write = [](const std::string&, const persistence::SchemaDescriptor&, const persistence::Snapshot&) {
            return PersistenceWriteResult{.ok = false, .code = "io_failure", .message = "disk full"};
        }});

    const auto outcome = generated_execute_save_request("slot1", 1, sample_schema(), sample_snapshot());
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "io_failure");
    CHECK(outcome.message == "disk full");
}

TEST_CASE("clearing a registered adapter restores adapter_unavailable", "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    register_persistence_adapter(PersistenceAdapter{
        .write = [](const std::string&, const persistence::SchemaDescriptor&, const persistence::Snapshot&) {
            return PersistenceWriteResult{.ok = true};
        }});
    clear_persistence_adapter();

    const auto outcome = generated_execute_save_request("slot1", 1, sample_schema(), sample_snapshot());
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "adapter_unavailable");
}

TEST_CASE("an adapter reporting a read failure passes that failure through unchanged",
          "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    register_persistence_adapter(PersistenceAdapter{
        .read = [](const std::string&, const persistence::SchemaDescriptor&) {
            return PersistenceReadResult{.ok = false, .code = "io_failure", .message = "slot missing"};
        }});

    const auto result = read_persistence_document("slot1", sample_schema());
    CHECK_FALSE(result.ok);
    CHECK(result.code == "io_failure");
    CHECK(result.message == "slot missing");
}

TEST_CASE("malformed adapter data — an adapter reporting success with an incompatible schema is rejected",
          "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    // The adapter claims success, but the document it hands back was written
    // against a different schema fingerprint than the one it's asked to
    // validate against — document validation stays runtime-owned regardless
    // of what the adapter itself believes.
    register_persistence_adapter(PersistenceAdapter{
        .read = [](const std::string&, const persistence::SchemaDescriptor&) {
            auto snapshot                = sample_snapshot();
            snapshot.schema_fingerprint += 1;
            return PersistenceReadResult{.ok = true, .snapshot = std::move(snapshot)};
        }});

    const auto result = read_persistence_document("slot1", sample_schema());
    CHECK_FALSE(result.ok);
    CHECK(result.code == "incompatible_schema");
}

TEST_CASE("incompatible descriptor on read — a stale revision is rejected the same way",
          "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    register_persistence_adapter(PersistenceAdapter{
        .read = [](const std::string&, const persistence::SchemaDescriptor&) {
            auto snapshot          = sample_snapshot();
            snapshot.schema_revision += 1;
            return PersistenceReadResult{.ok = true, .snapshot = std::move(snapshot)};
        }});

    const auto result = read_persistence_document("slot1", sample_schema());
    CHECK_FALSE(result.ok);
    CHECK(result.code == "incompatible_schema");
}

TEST_CASE("a write-then-read round trip through the same adapter preserves the exact document",
          "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    persistence::Snapshot stored;
    register_persistence_adapter(PersistenceAdapter{
        .write =
            [&](const std::string&, const persistence::SchemaDescriptor&, const persistence::Snapshot& snapshot) {
                stored = snapshot;  // an owned copy: the adapter never sees a live registry
                return PersistenceWriteResult{.ok = true};
            },
        .read = [&](const std::string&, const persistence::SchemaDescriptor&) {
            return PersistenceReadResult{.ok = true, .snapshot = stored};
        }});

    const auto snapshot = sample_snapshot();
    const auto outcome  = generated_execute_save_request("slot1", 1, sample_schema(), snapshot);
    REQUIRE(outcome.ok);

    const auto result = read_persistence_document("slot1", sample_schema());
    REQUIRE(result.ok);
    // Precision-preserving typed exchange: the decoded document equals the
    // one originally captured, field for field.
    CHECK(result.snapshot == snapshot);
}

TEST_CASE("a no-op restore of an empty-schema document succeeds", "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    register_persistence_adapter(PersistenceAdapter{
        .read = [](const std::string&, const persistence::SchemaDescriptor&) {
            return PersistenceReadResult{.ok = true, .snapshot = persistence::Snapshot{}};
        }});

    const auto outcome = execute_noop_restore_request("slot1", 7, persistence::SchemaDescriptor{});
    CHECK(outcome.ok);
    CHECK(outcome.slot == "slot1");
    CHECK(outcome.request_id == 7);
}

TEST_CASE("a no-op restore with no registered adapter fails as adapter_unavailable",
          "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    const auto outcome = execute_noop_restore_request("slot1", 7, persistence::SchemaDescriptor{});
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.slot == "slot1");
    CHECK(outcome.request_id == 7);
    CHECK(outcome.code == "adapter_unavailable");
}

TEST_CASE("a no-op restore rejects a document with a non-empty schema", "[runtime][persistence][adapter]") {
    ResetPersistenceState reset;

    register_persistence_adapter(PersistenceAdapter{
        .read = [](const std::string&, const persistence::SchemaDescriptor&) {
            return PersistenceReadResult{.ok = true, .snapshot = sample_snapshot()};
        }});

    const auto outcome = execute_noop_restore_request("slot1", 7, persistence::SchemaDescriptor{});
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.code == "incompatible_schema");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
