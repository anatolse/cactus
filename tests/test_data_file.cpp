// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/data_file.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace cactus;

// ── Test helpers ─────────────────────────────────────────────────────────────

static std::pair<ProgramNode, DecoratedProgram> compile(const std::string& source) {
    ErrorReporter errors;
    Lexer lexer(source, "test.cactus", errors);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(errors.has_errors());
    Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(program);
    REQUIRE_FALSE(errors.has_errors());
    return {std::move(program), std::move(decorated)};
}

// ── Task 6.1/6.3: Data file writer produces correct entity records ────────────

TEST_CASE("DataFile: simple unit — field values and trait mask", "[datafile]") {
    auto [program, decorated] = compile(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "entity Player:\n"
        "    Health:\n"
        "        hp = 42\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    REQUIRE(records.size() == 1);
    CHECK(records[0].name == "Player");
    // Should have hp field
    REQUIRE(records[0].fields.size() == 1);
    CHECK(records[0].fields[0].first == "hp");
    CHECK(records[0].fields[0].second.tag == FieldValue::Tag::Int);
    CHECK(records[0].fields[0].second.i32 == 42);
    // trait_mask: Health is bit 0
    CHECK(records[0].trait_mask == 0x1);
}

TEST_CASE("DataFile: float field with constant reference", "[datafile]") {
    auto [program, decorated] = compile(
        "const:\n"
        "    GRAVITY = 30.0\n"
        "trait Physics:\n"
        "    var gravity: float = 0.0\n"
        "entity Ball:\n"
        "    Physics:\n"
        "        gravity = GRAVITY\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    REQUIRE(records.size() == 1);
    CHECK(records[0].name == "Ball");
    REQUIRE(records[0].fields.size() == 1);
    CHECK(records[0].fields[0].second.tag == FieldValue::Tag::Float);
    CHECK(records[0].fields[0].second.f32 == 30.0F);
}

TEST_CASE("DataFile: vec2 field from constructor call", "[datafile]") {
    auto [program, decorated] = compile(
        "trait Position:\n"
        "    var pos: vec2\n"
        "entity Enemy:\n"
        "    Position:\n"
        "        pos = vec2(100.0, 200.0)\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].fields.size() == 1);
    CHECK(records[0].fields[0].second.tag == FieldValue::Tag::Vec2);
    CHECK(records[0].fields[0].second.vec2.x == 100.0F);
    CHECK(records[0].fields[0].second.vec2.y == 200.0F);
}

TEST_CASE("DataFile: bool field", "[datafile]") {
    auto [program, decorated] = compile(
        "trait Collectible:\n"
        "    var collected: bool = false\n"
        "entity Gem:\n"
        "    Collectible:\n"
        "        collected = false\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].fields.size() == 1);
    CHECK(records[0].fields[0].second.tag == FieldValue::Tag::Bool);
    CHECK(records[0].fields[0].second.b == false);
}

TEST_CASE("DataFile: color field from hex literal", "[datafile]") {
    auto [program, decorated] = compile(
        "trait Visual:\n"
        "    var color: color\n"
        "entity Block:\n"
        "    Visual:\n"
        "        color = #FF8800\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].fields.size() == 1);
    CHECK(records[0].fields[0].second.tag == FieldValue::Tag::Color);
    CHECK(records[0].fields[0].second.color.r == 0xFF);
    CHECK(records[0].fields[0].second.color.g == 0x88);
    CHECK(records[0].fields[0].second.color.bv == 0x00);
    CHECK(records[0].fields[0].second.color.a == 255);  // default alpha
}

// ── Task 6.4: Templates produce no data file entries ─────────────────────────

TEST_CASE("DataFile: template declarations excluded from data file", "[datafile]") {
    auto [program, decorated] = compile(
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "template Enemy:\n"
        "    Position\n"
        "entity Floor:\n"
        "    Position:\n"
        "        x = 0.0\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    // Only Floor unit, not Enemy template
    REQUIRE(records.size() == 1);
    CHECK(records[0].name == "Floor");
}

TEST_CASE("DataFile: multiple units all included", "[datafile]") {
    auto [program, decorated] = compile(
        "trait HP:\n"
        "    var hp: int = 100\n"
        "entity UnitA:\n"
        "    HP:\n"
        "        hp = 10\n"
        "entity UnitB:\n"
        "    HP:\n"
        "        hp = 20\n"
        "entity UnitC:\n"
        "    HP:\n"
        "        hp = 30\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    REQUIRE(records.size() == 3);
    CHECK(records[0].name == "UnitA");
    CHECK(records[1].name == "UnitB");
    CHECK(records[2].name == "UnitC");
    CHECK(records[0].fields[0].second.i32 == 10);
    CHECK(records[1].fields[0].second.i32 == 20);
    CHECK(records[2].fields[0].second.i32 == 30);
}

// ── Task 6.2: Disabled annotation sets bit unset in trait_mask ───────────────

TEST_CASE("DataFile: disabled trait not set in trait_mask", "[.datafile]") {
    auto [program, decorated] = compile(
        "trait Frozen\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "entity Enemy:\n"
        "    Position:\n"
        "        x = 1.0\n"
        "    Frozen: disabled\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    REQUIRE(records.size() == 1);
    // Position is bit 1 (Frozen is bit 0, declared first), Position is bit 1
    // trait_mask should have Position set but NOT Frozen
    uint64_t mask = records[0].trait_mask;
    // Frozen is bit 0 (declared first) - should NOT be set
    CHECK((mask & 0x1) == 0);
    // Position is bit 1 - should be set
    CHECK((mask & 0x2) != 0);
}

// ── Task 6.5/6.6/6.7: Round-trip write → read ───────────────────────────────

TEST_CASE("DataFile: write and read round-trip", "[datafile]") {
    auto [program, decorated] = compile(
        "trait Health:\n"
        "    var hp: int = 100\n"
        "trait Position:\n"
        "    var x: float = 0.0\n"
        "    var y: float = 0.0\n"
        "entity Player:\n"
        "    Health:\n"
        "        hp = 75\n"
        "    Position:\n"
        "        x = 10.0\n"
        "        y = 20.0\n");

    // Write
    auto tmp_dir = std::filesystem::temp_directory_path() / "cactus_test_data";
    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto written = writer.write(tmp_dir, "test_module");
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(written.size() == 1);

    // Read back
    auto data_path = tmp_dir / DataFileWriter::data_filename("test_module");
    DataFileReader reader(errors);
    auto loaded = reader.load(data_path);
    REQUIRE_FALSE(errors.has_errors());

    // Verify round-trip
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].name == "Player");
    CHECK(loaded[0].trait_mask == written[0].trait_mask);
    REQUIRE(loaded[0].fields.size() == written[0].fields.size());

    // Check hp field
    bool found_hp = false;
    bool found_x  = false;
    bool found_y  = false;
    for (auto& [fname, fval] : loaded[0].fields) {
        if (fname == "hp") {
            found_hp = true;
            CHECK(fval.tag == FieldValue::Tag::Int);
            CHECK(fval.i32 == 75);
        }
        if (fname == "x") {
            found_x = true;
            CHECK(fval.tag == FieldValue::Tag::Float);
            CHECK(fval.f32 == 10.0F);
        }
        if (fname == "y") {
            found_y = true;
            CHECK(fval.tag == FieldValue::Tag::Float);
            CHECK(fval.f32 == 20.0F);
        }
    }
    CHECK(found_hp);
    CHECK(found_x);
    CHECK(found_y);

    // Cleanup
    std::filesystem::remove_all(tmp_dir);
}

TEST_CASE("DataFile: version mismatch error", "[datafile]") {
    // Write a valid file, then manually corrupt the version
    auto [program, decorated] = compile(
        "trait HP:\n"
        "    var hp: int = 100\n"
        "entity P:\n"
        "    HP:\n"
        "        hp = 1\n");

    auto tmp_dir = std::filesystem::temp_directory_path() / "cactus_test_version";
    ErrorReporter write_errors;
    DataFileWriter writer(program, decorated, write_errors);
    writer.write(tmp_dir, "ver_test");
    REQUIRE_FALSE(write_errors.has_errors());

    // Corrupt the version bytes (bytes 4-5)
    auto data_path = tmp_dir / DataFileWriter::data_filename("ver_test");
    {
        std::fstream f(data_path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(4);  // After "CDAT" magic
        std::array<const char, 2> bad_version = {static_cast<char>(0xFF), static_cast<char>(0xFF)};
        f.write(bad_version.data(), bad_version.size());
    }

    // Should reject with version mismatch error
    ErrorReporter read_errors;
    DataFileReader reader(read_errors);
    auto loaded = reader.load(data_path);
    CHECK(read_errors.has_errors());
    CHECK(loaded.empty());

    std::filesystem::remove_all(tmp_dir);
}

TEST_CASE("DataFile: data_filename helper", "[datafile]") {
    CHECK(DataFileWriter::data_filename("levels.level1").string() == "levels.level1_data.bin");
    CHECK(DataFileWriter::data_filename("mini_shop").string() == "mini_shop_data.bin");
}

// ── Hierarchical entity templates (dsl-hierarchical-entity-templates D10) ───

TEST_CASE("DataFile: hierarchical entity serializes flat per-node records in preorder", "[datafile][hierarchy]") {
    auto [program, decorated] = compile(
        "trait Parent:\n"
        "    var parent: entity_id\n"
        "trait Tag:\n"
        "    var value: int = 0\n"
        "template Rig:\n"
        "    Tag\n"
        "    children:\n"
        "        entity Socket:\n"
        "            Tag:\n"
        "                value = 1\n"
        "            children:\n"
        "                entity Gem:\n"
        "                    Tag:\n"
        "                        value = 2\n"
        "entity Rig1 from Rig:\n"
        "    Tag:\n"
        "        value = 5\n");

    ErrorReporter errors;
    DataFileWriter writer(program, decorated, errors);
    auto records = writer.build_records();

    // One ordinary flat record per node, root first, preorder — no format change.
    REQUIRE(records.size() == 3);
    CHECK(records[0].name == "Rig1");
    CHECK(records[1].name == "Rig1.Socket");
    CHECK(records[2].name == "Rig1.Socket.Gem");

    // Node records carry their own flattened field values.
    REQUIRE(records[0].fields.size() == 1);
    CHECK(records[0].fields[0].second.i32 == 5);
    REQUIRE(records[1].fields.size() == 1);
    CHECK(records[1].fields[0].second.i32 == 1);
    REQUIRE(records[2].fields.size() == 1);
    CHECK(records[2].fields[0].second.i32 == 2);

    // No record-to-record Parent references appear in the data file.
    for (const auto& rec : records) {
        for (const auto& [field_name, value] : rec.fields) {
            CHECK(field_name != "parent");
        }
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
