// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/persistence_file_adapter.hpp"
#include "backends/cpp-entt/runtime.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace cactus::runtime::entt_backend;
namespace persistence = cactus::persistence;

namespace {

persistence::SchemaDescriptor sample_schema() {
    return persistence::SchemaDescriptor{.revision = cactus::kPersistenceSchemaRevision, .fingerprint = 42ULL};
}

persistence::Snapshot sample_snapshot() {
    persistence::Snapshot snapshot;
    snapshot.schema_revision    = cactus::kPersistenceSchemaRevision;
    snapshot.schema_fingerprint = 42ULL;
    snapshot.module             = "world";

    persistence::EntityRecord parent;
    parent.id        = 1;
    parent.archetype = "world.Boss";
    parent.traits.push_back(persistence::TraitRecord{
        .trait     = "world.Health",
        .persisted = {persistence::FieldValue{.name = "current", .value = persistence::Value::of_int(137)}}});

    persistence::EntityRecord child;
    child.id        = 2;
    child.archetype = "world.Badge";
    child.parent    = persistence::EntityRef{.id = 1, .present = true};
    child.traits.push_back(persistence::TraitRecord{
        .trait       = "world.Label",
        .construction = {persistence::FieldValue{.name = "text", .value = persistence::Value::of_string("elite")}},
        .persisted    = {persistence::FieldValue{.name = "glow", .value = persistence::Value::of_float(0.5F)}}});
    persistence::ListValue tags;
    tags.items.push_back(persistence::Value::of_enum("world.Rank", "Gold"));
    child.traits.back().persisted.push_back(
        persistence::FieldValue{.name = "tags", .value = persistence::Value::of_list(std::move(tags))});

    snapshot.entities.push_back(std::move(parent));
    snapshot.entities.push_back(std::move(child));
    return snapshot;
}

}  // namespace

TEST_CASE("a snapshot written through the file adapter reads back as an equal document",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());
    const auto snapshot = sample_snapshot();

    const auto write_result = adapter.write("slot1", sample_schema(), snapshot);
    REQUIRE(write_result.ok);
    REQUIRE(std::filesystem::exists(dir.path() / "slot1.cactussave"));

    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == snapshot);
}

TEST_CASE("reading a slot that was never written fails with io_failure", "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    const auto read_result = adapter.read("missing_slot", sample_schema());
    CHECK_FALSE(read_result.ok);
    CHECK(read_result.code == "io_failure");
}

TEST_CASE("writing a slot twice replaces the file and the second document is what's read back",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    auto first = sample_snapshot();
    REQUIRE(adapter.write("slot1", sample_schema(), first).ok);

    auto second             = sample_snapshot();
    second.entities.pop_back();
    REQUIRE(adapter.write("slot1", sample_schema(), second).ok);

    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == second);
    CHECK_FALSE(read_result.snapshot == first);
}

TEST_CASE("a write that cannot commit its temp file leaves the previously saved slot intact",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    const auto original = sample_snapshot();
    REQUIRE(adapter.write("slot1", sample_schema(), original).ok);

    // Force the next write's temp file open to fail by occupying its exact
    // path with a directory instead of a regular file.
    std::filesystem::create_directory(dir.path() / "slot1.cactussave.tmp");

    auto attempted = sample_snapshot();
    attempted.entities.pop_back();
    const auto write_result = adapter.write("slot1", sample_schema(), attempted);
    CHECK_FALSE(write_result.ok);
    CHECK(write_result.code == "io_failure");

    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == original);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
