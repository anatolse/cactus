#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "common/types.h"
#include "frontend/module_artifact.h"
#include "frontend/semantic_analyzer.h"

#include <filesystem>
#include <fstream>

using namespace cactus;
namespace fs = std::filesystem;

// ── Helpers ─────────────────────────────────────────────────────────────────

static fs::path test_build_dir() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR) / "artifact_test_build";
}

static DecoratedProgram make_test_program() {
    DecoratedProgram prog;

    // Trait: Position (pub)
    ResolvedTrait pos;
    pos.name = "Position";
    pos.is_pub = true;
    ResolvedField fx;
    fx.name = "x";
    fx.type = make_float_type();
    fx.is_var = true;
    fx.is_persist = true;
    fx.is_sync = true;
    ResolvedField fy;
    fy.name = "y";
    fy.type = make_float_type();
    fy.is_var = true;
    pos.fields = {fx, fy};
    prog.traits["Position"] = pos;

    // Trait: PlayerPhysics (non-pub)
    ResolvedTrait phys;
    phys.name = "PlayerPhysics";
    phys.is_pub = false;
    ResolvedField fmass;
    fmass.name = "mass";
    fmass.type = make_float_type();
    fmass.is_var = true;
    phys.fields = {fmass};
    prog.traits["PlayerPhysics"] = phys;

    // Enum: Direction
    ResolvedEnum dir;
    dir.name = "Direction";
    dir.variants = {"Up", "Down", "Left", "Right"};
    prog.enums["Direction"] = dir;

    // System dependency
    SystemDependency dep;
    dep.system_name = "MoveSystem";
    dep.reads = {"Position"};
    dep.writes = {"Position"};
    dep.emits = {};
    prog.dependency_graph.push_back(dep);

    prog.ast = nullptr;
    return prog;
}

// ── Artifact filename ────────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: artifact_filename simple", "[artifact]") {
    auto p = ModuleArtifact::artifact_filename("player");
    CHECK(p.string() == "player.cmod");
}

TEST_CASE("ModuleArtifact: artifact_filename dotted", "[artifact]") {
    auto p = ModuleArtifact::artifact_filename("enemies.walker");
    CHECK(p.string() == "enemies.walker.cmod");
}

// ── Round-trip ───────────────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: round-trip save and load", "[artifact]") {
    auto build_dir = test_build_dir();
    // Clean up first
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto program = make_test_program();

    // Save
    bool saved = artifact.save(program, "player", build_dir);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(saved);

    auto artifact_path = build_dir / "player.cmod";
    CHECK(fs::exists(artifact_path));

    // Load
    std::string loaded_name;
    auto loaded = artifact.load(artifact_path, loaded_name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());

    CHECK(loaded_name == "player");
    CHECK(loaded->traits.size() == 2);
    CHECK(loaded->enums.size() == 1);
    CHECK(loaded->dependency_graph.size() == 1);
    CHECK(loaded->ast == nullptr);

    // Verify trait data
    REQUIRE(loaded->traits.count("Position") == 1);
    auto& pos = loaded->traits.at("Position");
    CHECK(pos.is_pub);
    REQUIRE(pos.fields.size() == 2);
    CHECK(pos.fields[0].name == "x");
    CHECK(pos.fields[0].is_var);
    CHECK(pos.fields[0].is_persist);
    CHECK(pos.fields[0].is_sync);
    CHECK(pos.fields[0].type.kind == TypeKind::Float);

    // Verify non-pub trait
    REQUIRE(loaded->traits.count("PlayerPhysics") == 1);
    CHECK_FALSE(loaded->traits.at("PlayerPhysics").is_pub);

    // Verify enum
    REQUIRE(loaded->enums.count("Direction") == 1);
    CHECK(loaded->enums.at("Direction").variants.size() == 4);

    // Verify dep graph
    CHECK(loaded->dependency_graph[0].system_name == "MoveSystem");
    CHECK(loaded->dependency_graph[0].reads.count("Position") == 1);

    // Clean up
    fs::remove_all(build_dir, ec);
}

// ── Pub symbol extraction ────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: extract_pub_symbols only returns pub traits", "[artifact]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto program = make_test_program();

    artifact.save(program, "player", build_dir);
    REQUIRE_FALSE(errors.has_errors());

    auto path = build_dir / "player.cmod";
    auto symbols = artifact.extract_pub_symbols(path);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(symbols.has_value());

    CHECK(symbols->module_name == "player");
    // Only pub trait "Position" should be extracted
    CHECK(symbols->traits.size() == 1);
    REQUIRE(symbols->traits.count("Position") == 1);
    CHECK(symbols->traits.count("PlayerPhysics") == 0);

    // Enum should be present
    CHECK(symbols->enums.size() == 1);

    fs::remove_all(build_dir, ec);
}

// ── Build directory creation ─────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: creates build directory if missing", "[artifact]") {
    auto build_dir = test_build_dir() / "new_subdir";
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    REQUIRE_FALSE(fs::exists(build_dir));

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto program = make_test_program();

    bool saved = artifact.save(program, "test_module", build_dir);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(saved);
    CHECK(fs::exists(build_dir));
    CHECK(fs::exists(build_dir / "test_module.cmod"));

    fs::remove_all(test_build_dir(), ec);
}

// ── Overwrite existing artifact ──────────────────────────────────────────────

TEST_CASE("ModuleArtifact: overwrites existing artifact", "[artifact]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    auto program = make_test_program();

    // Save once
    artifact.save(program, "player", build_dir);
    auto path = build_dir / "player.cmod";
    auto size1 = fs::file_size(path);

    // Add another trait and save again
    ResolvedTrait extra;
    extra.name = "Extra";
    extra.is_pub = true;
    program.traits["Extra"] = extra;

    artifact.save(program, "player", build_dir);
    REQUIRE_FALSE(errors.has_errors());

    // File should exist and be different (larger)
    CHECK(fs::exists(path));
    auto size2 = fs::file_size(path);
    CHECK(size2 > size1);

    // Load and verify
    std::string name;
    auto loaded = artifact.load(path, name);
    REQUIRE(loaded.has_value());
    CHECK(loaded->traits.size() == 3);  // Position + PlayerPhysics + Extra

    fs::remove_all(build_dir, ec);
}

// ── Version mismatch ─────────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: version mismatch error", "[artifact]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    fs::create_directories(build_dir);

    // Write a file with wrong version manually
    auto path = build_dir / "stale.cmod";
    {
        std::ofstream out(path, std::ios::binary);
        out.write("CMOD", 4);
        uint8_t bad_version = 99;
        out.write(reinterpret_cast<const char*>(&bad_version), 1);
        // rest is garbage
    }

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    std::string name;
    auto result = artifact.load(path, name);
    CHECK_FALSE(result.has_value());
    CHECK(errors.has_errors());

    fs::remove_all(build_dir, ec);
}

// ── Invalid magic ─────────────────────────────────────────────────────────────

TEST_CASE("ModuleArtifact: invalid magic rejected", "[artifact]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    fs::create_directories(build_dir);

    auto path = build_dir / "bad.cmod";
    {
        std::ofstream out(path, std::ios::binary);
        out.write("JUNK", 4);
    }

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    std::string name;
    auto result = artifact.load(path, name);
    CHECK_FALSE(result.has_value());
    CHECK(errors.has_errors());

    fs::remove_all(build_dir, ec);
}
