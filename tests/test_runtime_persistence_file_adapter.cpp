// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/persistence_file_adapter.hpp"
#include "backends/cpp-entt/runtime.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
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
TEST_CASE("every value kind survives the file adapter round trip", "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    persistence::Snapshot snapshot;
    snapshot.schema_revision    = cactus::kPersistenceSchemaRevision;
    snapshot.schema_fingerprint = 0xFEDCBA9876543210ULL;
    snapshot.module             = "world";
    persistence::EntityRecord record;
    record.id        = 70000;
    record.archetype = "world.Everything";
    record.parent    = persistence::EntityRef{.id = 3, .present = false};
    persistence::StructValue nested;
    nested.fields.push_back(persistence::FieldValue{.name = "flag", .value = persistence::Value::of_bool(true)});
    persistence::ListValue items;
    items.items.push_back(persistence::Value::of_int(-2147483647 - 1));
    items.items.push_back(persistence::Value::of_int(2147483647));
    record.traits.push_back(persistence::TraitRecord{
        .trait        = "world.All",
        .present      = false,
        .construction = {persistence::FieldValue{.name = "negative", .value = persistence::Value::of_int(-5)},
                         persistence::FieldValue{.name  = "position",
                                                 .value = persistence::Value::of_vector(
                                                     cactus::PersistenceValueKind::Float, 3, {1.5F, -2.0F, 3.25F, 0.0F})},
                         persistence::FieldValue{.name  = "tint",
                                                 .value = persistence::Value::of_vector(
                                                     cactus::PersistenceValueKind::Int, 4, {1.0F, 2.0F, 3.0F, 255.0F})}},
        .persisted = {persistence::FieldValue{.name = "target",
                                              .value = persistence::Value::of_entity({.id = 1, .present = true})},
                      persistence::FieldValue{.name = "skin", .value = persistence::Value::of_asset("world.Stone")},
                      persistence::FieldValue{.name = "skin_unset", .value = persistence::Value::of_asset("")},
                      persistence::FieldValue{.name = "key", .value = persistence::Value::of_input("world.Jump")},
                      persistence::FieldValue{.name = "inner", .value = persistence::Value::of_struct(nested)},
                      persistence::FieldValue{.name = "items", .value = persistence::Value::of_list(items)},
                      persistence::FieldValue{.name = "label", .value = persistence::Value::of_string("")},
                      persistence::FieldValue{.name = "missing", .value = persistence::Value{}}}});
    snapshot.entities.push_back(record);

    REQUIRE(adapter.write("slot1", sample_schema(), snapshot).ok);
    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == snapshot);
}

TEST_CASE("repeated names are stored once, so many similar records stay small",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    persistence::Snapshot snapshot;
    snapshot.module = "world";
    constexpr int kRecords = 1000;
    for (int i = 0; i < kRecords; ++i) {
        persistence::EntityRecord record;
        record.id        = static_cast<persistence::DocumentId>(i + 1);
        record.archetype = "world.CellArchetype";
        record.traits.push_back(persistence::TraitRecord{
            .trait        = "world.CellPosition",
            .construction = {persistence::FieldValue{.name = "x", .value = persistence::Value::of_int(i % 64)},
                             persistence::FieldValue{.name = "y", .value = persistence::Value::of_int(-(i % 16))},
                             persistence::FieldValue{.name = "z", .value = persistence::Value::of_int(i / 64)}}});
        snapshot.entities.push_back(std::move(record));
    }

    REQUIRE(adapter.write("slot1", sample_schema(), snapshot).ok);
    const auto bytes = std::filesystem::file_size(dir.path() / "slot1.cactussave");
    CHECK(bytes < static_cast<std::uintmax_t>(kRecords) * 24U);
    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == snapshot);
}

TEST_CASE("a field value repeated from the previous record costs about a byte",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    persistence::Snapshot snapshot;
    snapshot.module = "world";
    constexpr int kRecords = 1000;
    for (int i = 0; i < kRecords; ++i) {
        persistence::EntityRecord record;
        record.id        = static_cast<persistence::DocumentId>(i + 1);
        record.archetype = "world.CellArchetype";
        const auto shared = persistence::Value::of_vector(cactus::PersistenceValueKind::Float, 4, {0.0F, 0.0F, 0.0F, 1.0F});
        record.traits.push_back(persistence::TraitRecord{
            .trait        = "world.Transform",
            .construction = {persistence::FieldValue{.name = "rotation", .value = shared},
                             persistence::FieldValue{.name = "scale", .value = shared}},
            .persisted    = {persistence::FieldValue{.name = "rotation", .value = persistence::Value::of_int(i)}}});
        snapshot.entities.push_back(std::move(record));
    }

    REQUIRE(adapter.write("slot1", sample_schema(), snapshot).ok);
    const auto bytes = std::filesystem::file_size(dir.path() / "slot1.cactussave");
    CHECK(bytes < static_cast<std::uintmax_t>(kRecords) * 20U);
    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == snapshot);
}

TEST_CASE("a truncated save fails to read with io_failure", "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());
    REQUIRE(adapter.write("slot1", sample_schema(), sample_snapshot()).ok);

    const auto path = dir.path() / "slot1.cactussave";
    std::filesystem::resize_file(path, std::filesystem::file_size(path) / 2);
    const auto read_result = adapter.read("slot1", sample_schema());
    CHECK_FALSE(read_result.ok);
    CHECK(read_result.code == "io_failure");
}

TEST_CASE("a negative zero after a positive zero keeps its sign", "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    persistence::Snapshot snapshot;
    snapshot.module = "world";
    for (const float value : {0.0F, -0.0F}) {
        persistence::EntityRecord record;
        record.id        = static_cast<persistence::DocumentId>(snapshot.entities.size() + 1);
        record.archetype = "world.Cell";
        record.traits.push_back(persistence::TraitRecord{
            .trait     = "world.Motion",
            .persisted = {persistence::FieldValue{.name = "speed", .value = persistence::Value::of_float(value)},
                          persistence::FieldValue{.name  = "velocity",
                                                  .value = persistence::Value::of_vector(
                                                      cactus::PersistenceValueKind::Float, 3, {value, 1.0F, 2.0F, 0.0F})}}});
        snapshot.entities.push_back(std::move(record));
    }

    REQUIRE(adapter.write("slot1", sample_schema(), snapshot).ok);
    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    const auto& fields = read_result.snapshot.entities.at(1).traits.at(0).persisted;
    CHECK(std::signbit(fields.at(0).value.as_float()));
    CHECK(std::signbit(fields.at(1).value.as_vector().components[0]));
}

TEST_CASE("a save nesting values without bound fails to read instead of overflowing the stack",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    std::string bytes = "CPST";
    bytes += '\x02';                                // format
    bytes += static_cast<char>(cactus::kPersistenceSchemaRevision);
    bytes += '\x2A';                                // fingerprint
    bytes += std::string("\x00\x01w", 3);           // module name
    bytes += '\x01';                                // one entity
    bytes += '\x01';                                // id
    bytes += std::string("\x00\x01" "a", 3);        // archetype
    bytes += std::string("\x00\x00", 2);            // parent
    bytes += '\x01';                                // one trait
    bytes += std::string("\x00\x01t", 3);           // trait name
    bytes += '\x01';                                // present
    bytes += '\x01';                                // one construction field
    bytes += std::string("\x00\x01" "f", 3);        // field name
    const auto list = static_cast<char>(cactus::PersistenceValueKind::List);
    for (int level = 0; level < 200000; ++level) {
        bytes += list;
        bytes += '\x01';
    }
    {
        std::ofstream out(dir.path() / "slot1.cactussave", std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto read_result = adapter.read("slot1", sample_schema());
    CHECK_FALSE(read_result.ok);
    CHECK(read_result.code == "io_failure");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
