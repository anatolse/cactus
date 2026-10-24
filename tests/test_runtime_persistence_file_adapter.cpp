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
#include <vector>

using namespace cactus::runtime::entt_backend;
namespace persistence = cactus::persistence;
using persistence::StructField;
using persistence::Value;

namespace {

persistence::SchemaDescriptor sample_schema() {
    return persistence::SchemaDescriptor{.revision = cactus::kPersistenceSchemaRevision, .fingerprint = 42ULL};
}

persistence::SnapshotBuilder sample_builder() {
    persistence::SnapshotBuilder builder;
    builder.set_schema(cactus::kPersistenceSchemaRevision, 42ULL);
    builder.set_module("world");
    builder.add_entity(1, "world.Boss");
    builder.add_trait("world.Health", {}, {{.name = "current", .value = Value::of_int(137)}});
    return builder;
}

persistence::Snapshot sample_snapshot(bool with_child = true) {
    auto builder = sample_builder();
    if (with_child) {
        persistence::ListValue tags;
        tags.items.push_back(Value::of_enum("world.Rank", "Gold"));
        builder.add_entity(2, "world.Badge", persistence::EntityRef{.id = 1, .present = true});
        builder.add_trait("world.Label", {{.name = "text", .value = Value::of_string("elite")}},
                          {{.name = "glow", .value = Value::of_float(0.5F)},
                           {.name = "tags", .value = Value::of_list(std::move(tags))}});
    }
    return std::move(builder).finish();
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
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

    const auto first = sample_snapshot();
    REQUIRE(adapter.write("slot1", sample_schema(), first).ok);

    const auto second = sample_snapshot(false);
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

    const auto write_result = adapter.write("slot1", sample_schema(), sample_snapshot(false));
    CHECK_FALSE(write_result.ok);
    CHECK(write_result.code == "io_failure");

    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == original);
}

TEST_CASE("every value kind survives the file adapter round trip", "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    persistence::SnapshotBuilder builder;
    builder.set_schema(cactus::kPersistenceSchemaRevision, 0xFEDCBA9876543210ULL);
    builder.set_module("world");
    builder.mark_truncated();
    builder.add_entity(70000, "world.Everything", persistence::EntityRef{.id = 3, .present = false});
    persistence::StructValue nested;
    nested.fields.push_back(StructField{.name = "flag", .value = Value::of_bool(true)});
    persistence::ListValue items;
    items.items.push_back(Value::of_int(-2147483647 - 1));
    items.items.push_back(Value::of_int(2147483647));
    persistence::ListValue mixed;
    mixed.items.push_back(Value::of_int(1));
    mixed.items.push_back(Value::of_string("two"));
    builder.add_absent_trait("world.Gone");
    builder.add_trait(
        "world.All",
        {{.name = "negative", .value = Value::of_int(-5)},
         {.name  = "position",
          .value = Value::of_vector(cactus::PersistenceValueKind::Float, 3, {1.5F, -2.0F, 3.25F, 0.0F})},
         {.name  = "tint",
          .value = Value::of_vector(cactus::PersistenceValueKind::Int, 4, {1.0F, 2.0F, 3.0F, 255.0F})}},
        {{.name = "target", .value = Value::of_entity({.id = 1, .present = true})},
         {.name = "skin", .value = Value::of_asset("world.Stone")},
         {.name = "skin_unset", .value = Value::of_asset("")},
         {.name = "key", .value = Value::of_input("world.Jump")},
         {.name = "stance", .value = Value::of_enum("world.Stance", "Guarding")},
         {.name = "inner", .value = Value::of_struct(nested)},
         {.name = "items", .value = Value::of_list(items)},
         {.name = "mixed", .value = Value::of_list(mixed)},
         {.name = "label", .value = Value::of_string("")},
         {.name = "missing", .value = Value{}}});
    const auto snapshot = std::move(builder).finish();

    REQUIRE(adapter.write("slot1", sample_schema(), snapshot).ok);
    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == snapshot);
    CHECK(read_result.snapshot.truncated());
}

TEST_CASE("repeated names are stored once, so many similar records stay small",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    persistence::SnapshotBuilder builder;
    builder.set_module("world");
    constexpr int kRecords = 1000;
    for (int i = 0; i < kRecords; ++i) {
        builder.add_entity(static_cast<persistence::DocumentId>(i + 1), "world.CellArchetype");
        builder.add_trait("world.CellPosition", {{.name = "x", .value = Value::of_int(i % 64)},
                                                 {.name = "y", .value = Value::of_int(-(i % 16))},
                                                 {.name = "z", .value = Value::of_int(i / 64)}});
    }
    const auto snapshot = std::move(builder).finish();

    REQUIRE(adapter.write("slot1", sample_schema(), snapshot).ok);
    const auto bytes = std::filesystem::file_size(dir.path() / "slot1.cactussave");
    CHECK(bytes < static_cast<std::uintmax_t>(kRecords) * 12U);
    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    CHECK(read_result.snapshot == snapshot);
}

TEST_CASE("a field value repeated from the previous record costs about a bit",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    persistence::SnapshotBuilder builder;
    builder.set_module("world");
    constexpr int kRecords = 1000;
    const auto shared = Value::of_vector(cactus::PersistenceValueKind::Float, 4, {0.0F, 0.0F, 0.0F, 1.0F});
    for (int i = 0; i < kRecords; ++i) {
        builder.add_entity(static_cast<persistence::DocumentId>(i + 1), "world.CellArchetype");
        builder.add_trait("world.Transform",
                          {{.name = "rotation", .value = shared}, {.name = "scale", .value = shared}},
                          {{.name = "turn", .value = Value::of_int(i)}});
    }
    const auto snapshot = std::move(builder).finish();

    REQUIRE(adapter.write("slot1", sample_schema(), snapshot).ok);
    const auto bytes = std::filesystem::file_size(dir.path() / "slot1.cactussave");
    CHECK(bytes < static_cast<std::uintmax_t>(kRecords) * 12U);
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

TEST_CASE("a save in the previous file format is rejected as incompatible", "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    std::string bytes = "CPST";
    bytes += '\x02';  // format 2
    bytes += static_cast<char>(cactus::kPersistenceSchemaRevision);
    bytes += '\x2A';
    write_bytes(dir.path() / "slot1.cactussave", bytes);

    const auto read_result = adapter.read("slot1", sample_schema());
    CHECK_FALSE(read_result.ok);
    CHECK(read_result.code == "incompatible_schema");
    CHECK(read_result.message.contains("format 2"));
}

TEST_CASE("a negative zero after a positive zero keeps its sign", "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    persistence::SnapshotBuilder builder;
    builder.set_module("world");
    persistence::DocumentId id = 0;
    for (const float value : {0.0F, -0.0F}) {
        builder.add_entity(++id, "world.Cell");
        builder.add_trait(
            "world.Motion", {},
            {{.name = "speed", .value = Value::of_float(value)},
             {.name  = "velocity",
              .value = Value::of_vector(cactus::PersistenceValueKind::Float, 3, {value, 1.0F, 2.0F, 0.0F})}});
    }

    REQUIRE(adapter.write("slot1", sample_schema(), std::move(builder).finish()).ok);
    const auto read_result = adapter.read("slot1", sample_schema());
    REQUIRE(read_result.ok);
    const auto motion = read_result.snapshot.entity(1).trait("world.Motion");
    REQUIRE(motion.has_value());
    CHECK(std::signbit(motion->field("speed")->as_float()));
    CHECK(std::signbit(motion->field("velocity")->as_vector().components[0]));
}

TEST_CASE("a save nesting values without bound fails to read instead of overflowing the stack",
          "[runtime][persistence][adapter][file]") {
    cactus_test::ScopedTempDirectory dir;
    const auto adapter = make_example_file_adapter(dir.path());

    constexpr int kLevels = 200000;
    std::string bytes = "CPST";
    bytes += '\x03';  // format
    bytes += static_cast<char>(cactus::kPersistenceSchemaRevision);
    bytes += '\x2A';                        // fingerprint
    bytes += std::string("\x01w", 2);       // module
    bytes += '\x00';                        // not truncated
    bytes += '\x03';                        // names
    bytes += std::string("\x01" "a\x01t\x01" "f", 6);
    // A chain of list types, each listing the next, ending in an int.
    const auto varint = [&](std::uint64_t value) {
        while (value >= 0x80U) {
            bytes += static_cast<char>((value & 0x7FU) | 0x80U);
            value >>= 7U;
        }
        bytes += static_cast<char>(value);
    };
    varint(kLevels + 1);
    for (int level = 0; level < kLevels; ++level) {
        bytes += static_cast<char>(cactus::PersistenceValueKind::List);
        bytes += std::string("\x00\x00", 2);
        varint(static_cast<std::uint64_t>(level) + 2);
    }
    bytes += static_cast<char>(cactus::PersistenceValueKind::Int);
    bytes += std::string("\x00\x00\x00", 3);
    bytes += '\x00';                         // no structs
    bytes += '\x00';                         // no enums
    bytes += '\x01';                         // one trait layout
    bytes += std::string("\x01\x01\x02\x00\x00", 5);  // "t": construction field "f" of type 0; no persisted
    bytes += '\x01';                         // one entity
    bytes += '\x02';                         // id 1
    bytes += std::string("\x00\x00\x00", 3);  // archetype "a", no parent
    bytes += std::string("\x01\x00\x01\x00", 4);  // one trait row: layout 0, present, no repeats
    for (int level = 0; level < kLevels; ++level) {
        bytes += '\x01';
    }
    write_bytes(dir.path() / "slot1.cactussave", bytes);

    const auto read_result = adapter.read("slot1", sample_schema());
    CHECK_FALSE(read_result.ok);
    CHECK(read_result.code == "io_failure");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
