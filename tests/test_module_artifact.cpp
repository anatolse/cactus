// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "common/types.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

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
    pos.name   = "Position";
    pos.is_pub = true;
    ResolvedField fx;
    fx.name       = "x";
    fx.type       = make_float_type();
    fx.is_var     = true;
    fx.is_persist = true;
    fx.is_sync    = true;
    ResolvedField fy;
    fy.name                 = "y";
    fy.type                 = make_float_type();
    fy.is_var               = true;
    pos.fields              = {fx, fy};
    prog.traits["Position"] = pos;

    // Trait: PlayerPhysics (non-pub)
    ResolvedTrait phys;
    phys.name   = "PlayerPhysics";
    phys.is_pub = false;
    ResolvedField fmass;
    fmass.name                   = "mass";
    fmass.type                   = make_float_type();
    fmass.is_var                 = true;
    phys.fields                  = {fmass};
    prog.traits["PlayerPhysics"] = phys;

    // Enum: Direction
    ResolvedEnum dir;
    dir.name                = "Direction";
    dir.variants            = {"Up", "Down", "Left", "Right"};
    prog.enums["Direction"] = dir;

    // System dependency
    SystemDependency dep;
    dep.system_name = "MoveSystem";
    dep.reads       = {"Position"};
    dep.writes      = {"Position"};
    dep.emits       = {};
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

    auto path    = build_dir / "player.cmod";
    auto symbols = artifact.extract_pub_symbols(path);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(symbols.has_value());

    CHECK(symbols->module_name == "player");
    // Only pub trait "Position" should be extracted
    CHECK(symbols->traits.size() == 1);
    REQUIRE(symbols->traits.contains("Position"));
    CHECK_FALSE(symbols->traits.contains("PlayerPhysics"));

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
    auto path  = build_dir / "player.cmod";
    auto size1 = fs::file_size(path);

    // Add another trait and save again
    ResolvedTrait extra;
    extra.name              = "Extra";
    extra.is_pub            = true;
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
        const char bad_version = 99;
        out.write(&bad_version, 1);
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

// ── Funcs round-trip (task 5.7) ─────────────────────────────────────────────

TEST_CASE("ModuleArtifact: extern func round-trips through save/load", "[artifact][extern-func]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    DecoratedProgram prog = make_test_program();

    // Add an extern func
    ResolvedFunc lerp;
    lerp.name      = "lerp";
    lerp.is_pub    = true;
    lerp.is_extern = true;
    ResolvedParam pa;
    pa.name = "a";
    pa.type = make_float_type();
    lerp.params.push_back(pa);
    ResolvedParam pb;
    pb.name = "b";
    pb.type = make_float_type();
    lerp.params.push_back(pb);
    ResolvedParam pt;
    pt.name = "t";
    pt.type = make_float_type();
    lerp.params.push_back(pt);
    lerp.return_type   = make_float_type();
    prog.funcs["lerp"] = lerp;

    // Also add a non-pub func
    ResolvedFunc helper;
    helper.name                   = "internal_helper";
    helper.is_pub                 = false;
    helper.is_extern              = true;
    prog.funcs["internal_helper"] = helper;

    artifact.save(prog, "mathlib", build_dir);
    REQUIRE_FALSE(errors.has_errors());

    auto path = build_dir / "mathlib.cmod";
    std::string name;
    auto loaded = artifact.load(path, name);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(loaded.has_value());

    CHECK(name == "mathlib");
    REQUIRE(loaded->funcs.contains("lerp"));
    auto& lf = loaded->funcs.at("lerp");
    CHECK(lf.name == "lerp");
    CHECK(lf.is_pub);
    CHECK(lf.is_extern);
    REQUIRE(lf.params.size() == 3);
    CHECK(lf.params[0].name == "a");
    CHECK(lf.params[0].type.kind == TypeKind::Float);
    REQUIRE(lf.return_type.has_value());
    CHECK(lf.return_type->kind == TypeKind::Float);

    // Non-pub func also round-trips
    REQUIRE(loaded->funcs.contains("internal_helper"));

    fs::remove_all(build_dir, ec);
}

TEST_CASE("ModuleArtifact: extract_pub_symbols returns pub extern funcs only", "[artifact][extern-func]") {
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    DecoratedProgram prog = make_test_program();

    // Add pub extern func
    ResolvedFunc pub_func;
    pub_func.name      = "lerp";
    pub_func.is_pub    = true;
    pub_func.is_extern = true;
    prog.funcs["lerp"] = pub_func;

    // Add non-pub extern func
    ResolvedFunc priv_func;
    priv_func.name         = "internal";
    priv_func.is_pub       = false;
    priv_func.is_extern    = true;
    prog.funcs["internal"] = priv_func;

    artifact.save(prog, "testmod", build_dir);
    REQUIRE_FALSE(errors.has_errors());

    auto path    = build_dir / "testmod.cmod";
    auto symbols = artifact.extract_pub_symbols(path);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(symbols.has_value());

    // Only pub func should be in symbols
    CHECK(symbols->funcs.size() == 1);
    REQUIRE(symbols->funcs.contains("lerp"));
    CHECK_FALSE(symbols->funcs.contains("internal"));
    CHECK(symbols->funcs.at("lerp").is_pub);
    CHECK(symbols->funcs.at("lerp").is_extern);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("ModuleArtifact: version-1 artifact rejected with error", "[artifact][extern-func]") {
    // This is covered by the existing "version mismatch error" test (version 99)
    // which also rejects version 1 (since CURRENT_VERSION is now 2)
    auto build_dir = test_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    fs::create_directories(build_dir);

    // Write a file with version 1 (old format, no funcs section)
    auto path = build_dir / "v1.cmod";
    {
        std::ofstream out(path, std::ios::binary);
        out.write("CMOD", 4);
        const char v1 = 1;
        out.write(&v1, 1);
        // rest doesn't matter
    }

    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    std::string name;
    auto result = artifact.load(path, name);
    CHECK_FALSE(result.has_value());
    CHECK(errors.has_errors());  // version 1 != CURRENT_VERSION(2) → error

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
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
