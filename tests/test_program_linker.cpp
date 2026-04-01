#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "frontend/module_artifact.h"
#include "frontend/program_linker.h"
#include "frontend/semantic_analyzer.h"

#include <filesystem>

using namespace cactus;
namespace fs = std::filesystem;

static fs::path linker_build_dir() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR) / "linker_test_build";
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static DecoratedProgram make_program(
    const std::string& trait_name, bool is_pub,
    const std::string& enum_name = "") {
    DecoratedProgram prog;

    ResolvedTrait rt;
    rt.name = trait_name;
    rt.is_pub = is_pub;
    ResolvedField f;
    f.name = "x";
    f.type = make_float_type();
    f.is_var = true;
    rt.fields.push_back(f);
    prog.traits[trait_name] = rt;

    if (!enum_name.empty()) {
        ResolvedEnum re;
        re.name = enum_name;
        re.variants = {"A", "B"};
        prog.enums[enum_name] = re;
    }

    return prog;
}

// ── Task 5.2: Incremental merge — distinct types ──────────────────────────────

TEST_CASE("program_linker: merge two programs with distinct traits", "[linker][5.2]") {
    auto prog_a = make_program("Position", true);
    auto prog_b = make_program("EnemyAI", true);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, prog_a, "player"));
    REQUIRE(linker.merge_into(merged, prog_b, "enemies"));

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.traits.count("Position") == 1);
    CHECK(merged.traits.count("EnemyAI") == 1);
}

TEST_CASE("program_linker: merge includes enums from all modules", "[linker][5.2]") {
    auto prog_a = make_program("Position", true, "Direction");
    auto prog_b = make_program("Velocity", true, "State");

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, prog_a, "player"));
    REQUIRE(linker.merge_into(merged, prog_b, "physics"));

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.enums.count("Direction") == 1);
    CHECK(merged.enums.count("State") == 1);
}

TEST_CASE("program_linker: dependency graphs are concatenated", "[linker][5.2]") {
    DecoratedProgram prog_a;
    SystemDependency dep_a;
    dep_a.system_name = "MoveSystem";
    dep_a.reads.insert("Position");
    prog_a.dependency_graph.push_back(dep_a);

    DecoratedProgram prog_b;
    SystemDependency dep_b;
    dep_b.system_name = "RenderSystem";
    dep_b.reads.insert("Sprite");
    prog_b.dependency_graph.push_back(dep_b);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    linker.merge_into(merged, prog_a, "player");
    linker.merge_into(merged, prog_b, "render");

    CHECK_FALSE(errors.has_errors());
    CHECK(merged.dependency_graph.size() == 2);
    CHECK(merged.dependency_graph[0].system_name == "MoveSystem");
    CHECK(merged.dependency_graph[1].system_name == "RenderSystem");
}

// ── Task 5.3: Duplicate pub symbol detection ──────────────────────────────────

TEST_CASE("program_linker: duplicate pub trait name detected", "[linker][5.3]") {
    auto prog_a = make_program("Position", true);
    auto prog_b = make_program("Position", true);  // same pub name!

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    linker.merge_into(merged, prog_a, "modA");
    bool ok = linker.merge_into(merged, prog_b, "modB");

    CHECK_FALSE(ok);
    CHECK(errors.has_errors());
    const auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("duplicate symbol") != std::string::npos);
    CHECK(msg.find("Position") != std::string::npos);
    CHECK(msg.find("modA") != std::string::npos);
    CHECK(msg.find("modB") != std::string::npos);
}

TEST_CASE("program_linker: duplicate enum name detected", "[linker][5.3]") {
    auto prog_a = make_program("TraitA", true, "Direction");
    auto prog_b = make_program("TraitB", true, "Direction");  // same enum name

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    linker.merge_into(merged, prog_a, "modA");
    bool ok = linker.merge_into(merged, prog_b, "modB");

    CHECK_FALSE(ok);
    CHECK(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("Direction") != std::string::npos);
}

TEST_CASE("program_linker: non-pub trait with same name does not conflict", "[linker][5.3]") {
    // Non-pub traits don't enter the global namespace, so no conflict
    auto prog_a = make_program("InternalHelper", false);  // not pub
    auto prog_b = make_program("InternalHelper", false);  // not pub, same name

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    REQUIRE(linker.merge_into(merged, prog_a, "modA"));
    REQUIRE(linker.merge_into(merged, prog_b, "modB"));

    // No error — both are non-pub
    CHECK_FALSE(errors.has_errors());
    // The second one overwrites the first (implementation detail, both are non-pub)
    CHECK(merged.traits.count("InternalHelper") == 1);
}

// ── Task 5.5: String pool merged ─────────────────────────────────────────────

TEST_CASE("program_linker: string pool contains symbol names after merge", "[linker][5.5]") {
    auto prog_a = make_program("Position", true);
    auto prog_b = make_program("Velocity", true);

    ErrorReporter errors;
    ProgramLinker linker(errors);

    DecoratedProgram merged;
    linker.merge_into(merged, prog_a, "player");
    linker.merge_into(merged, prog_b, "physics");

    // The pool should have the symbol names interned
    CHECK(merged.string_pool.contains("Position"));
    CHECK(merged.string_pool.contains("Velocity"));
}

// ── Task 5.1: Link from .cmod artifact files ──────────────────────────────────

TEST_CASE("program_linker: link from artifact files round-trip", "[linker][5.1]") {
    auto build_dir = linker_build_dir();
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Save two programs as artifacts
    auto prog_a = make_program("Position", true);
    auto prog_b = make_program("EnemyAI", true);

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    REQUIRE(artifact.save(prog_a, "player", build_dir));
    REQUIRE(artifact.save(prog_b, "enemies", build_dir));
    REQUIRE_FALSE(save_errors.has_errors());

    // Link them
    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "player.cmod",
                                build_dir / "enemies.cmod"});

    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());
    CHECK(merged->traits.count("Position") == 1);
    CHECK(merged->traits.count("EnemyAI") == 1);
    CHECK(merged->ast == nullptr);  // AST not preserved in artifacts

    fs::remove_all(build_dir, ec);
}

TEST_CASE("program_linker: link detects duplicate across artifacts", "[linker][5.1]") {
    auto build_dir = linker_build_dir() / "dup";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Both modules define pub trait "Config"
    auto prog_a = make_program("Config", true);
    auto prog_b = make_program("Config", true);

    ErrorReporter save_errors;
    ModuleArtifact artifact(save_errors);
    artifact.save(prog_a, "modA", build_dir);
    artifact.save(prog_b, "modB", build_dir);

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "modA.cmod",
                                build_dir / "modB.cmod"});

    CHECK_FALSE(merged.has_value());
    CHECK(link_errors.has_errors());

    fs::remove_all(build_dir, ec);
}
