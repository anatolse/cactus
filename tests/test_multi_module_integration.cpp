#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "frontend/lexer.h"
#include "frontend/module_artifact.h"
#include "frontend/module_resolver.h"
#include "frontend/parser.h"
#include "frontend/program_linker.h"
#include "frontend/semantic_analyzer.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace cactus;
namespace fs = std::filesystem;

static fs::path fixtures_dir() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR) / "multi_module";
}

static fs::path integration_build_dir() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR) / "integration_build";
}

// ── Helper: compile a single .cactus file ──────────────────────────────────

/// Lex, parse and semantically analyze one file with optional imports.
/// Returns (program, decorated) or reports errors.
static std::optional<DecoratedProgram> compile_file(const fs::path& path,
                                                      ErrorReporter& errors,
                                                      const ModuleImports& imports = {}) {
    // Read source
    std::ifstream ifs(path);
    if (!ifs) {
        errors.error({}, "cannot open file: " + path.string());
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string source = ss.str();

    // Lex
    Lexer lexer(source, path.string(), errors);
    auto tokens = lexer.tokenize();
    if (errors.has_errors()) return std::nullopt;

    // Parse
    Parser parser(std::move(tokens), errors);
    auto program_node = parser.parse_program();
    if (errors.has_errors()) return std::nullopt;

    // Semantic analyze
    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(program_node, imports);
    if (errors.has_errors()) return std::nullopt;

    return decorated;
}

// ── Task 7.2: End-to-end integration tests ────────────────────────────────────

TEST_CASE("integration: compile player.cactus and save artifact", "[integration][7.2]") {
    auto build_dir = integration_build_dir() / "player_only";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    ErrorReporter errors;
    auto decorated = compile_file(fixtures_dir() / "player.cactus", errors);

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(decorated.has_value());
    CHECK(decorated->traits.count("Position") == 1);
    CHECK(decorated->traits.at("Position").is_pub);

    // Save artifact
    ModuleArtifact artifact(errors);
    REQUIRE(artifact.save(*decorated, "player", build_dir));
    REQUIRE_FALSE(errors.has_errors());
    CHECK(fs::exists(build_dir / "player.cmod"));

    fs::remove_all(build_dir, ec);
}

TEST_CASE("integration: resolve → compile → link player + level", "[integration][7.2]") {
    auto build_dir = integration_build_dir() / "player_level";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Step 1: Compile player.cactus (no dependencies)
    ErrorReporter player_errors;
    auto player_prog = compile_file(fixtures_dir() / "player.cactus", player_errors);
    REQUIRE_FALSE(player_errors.has_errors());
    REQUIRE(player_prog.has_value());

    // Step 2: Save player artifact
    ModuleArtifact artifact(player_errors);
    REQUIRE(artifact.save(*player_prog, "player", build_dir));

    // Step 3: Build imports for level (player is a dependency)
    ImportedSymbols player_syms;
    player_syms.module_name = "player";
    for (auto& [name, trait] : player_prog->traits) {
        if (trait.is_pub) player_syms.traits[name] = trait;
    }
    ModuleImports level_imports;
    level_imports.add("player", std::move(player_syms));

    // Step 4: Compile level.cactus with player imports
    ErrorReporter level_errors;
    auto level_prog = compile_file(fixtures_dir() / "level.cactus", level_errors, level_imports);
    REQUIRE_FALSE(level_errors.has_errors());
    REQUIRE(level_prog.has_value());
    CHECK(level_prog->traits.count("LevelData") == 1);

    // Step 5: Save level artifact
    ModuleArtifact level_artifact(level_errors);
    REQUIRE(level_artifact.save(*level_prog, "level", build_dir));

    // Step 6: Link player.cmod + level.cmod
    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "player.cmod",
                                build_dir / "level.cmod"});

    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());
    CHECK(merged->traits.count("Position") == 1);
    CHECK(merged->traits.count("LevelData") == 1);
    CHECK(merged->traits.at("Position").is_pub);
    CHECK(merged->traits.at("LevelData").is_pub);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("integration: module resolver produces topo order for multi_module fixtures",
          "[integration][7.2]") {
    // ModuleResolver on level.cactus (which depends on player)
    ErrorReporter errors;
    ModuleResolver resolver(errors);
    auto modules = resolver.resolve(fixtures_dir() / "level.cactus");

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(modules.size() >= 2);  // at least player + level

    // player must come before level in topo order (dependencies first)
    size_t player_idx = SIZE_MAX;
    size_t level_idx  = SIZE_MAX;
    for (size_t i = 0; i < modules.size(); ++i) {
        if (modules[i].qualified_name == "player") player_idx = i;
        if (modules[i].qualified_name == "level")  level_idx  = i;
    }
    REQUIRE(player_idx != SIZE_MAX);
    REQUIRE(level_idx != SIZE_MAX);
    CHECK(player_idx < level_idx);
}

TEST_CASE("integration: full pipeline using module resolver", "[integration][7.2]") {
    auto build_dir = integration_build_dir() / "full_pipeline";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Resolve from level.cactus (which uses player)
    ErrorReporter resolve_errors;
    ModuleResolver resolver(resolve_errors);
    auto modules = resolver.resolve(fixtures_dir() / "level.cactus");
    REQUIRE_FALSE(resolve_errors.has_errors());
    REQUIRE_FALSE(modules.empty());

    // Compile modules in topo order (leaves first)
    std::unordered_map<std::string, DecoratedProgram> compiled;
    std::vector<fs::path> artifact_paths;

    for (auto& mod : modules) {
        // Build imports from already-compiled dependencies
        ModuleImports imports;
        for (auto& dep_name : mod.dependencies) {
            auto it = compiled.find(dep_name);
            if (it != compiled.end()) {
                ImportedSymbols syms;
                syms.module_name = dep_name;
                for (auto& [name, trait] : it->second.traits) {
                    if (trait.is_pub) syms.traits[name] = trait;
                }
                for (auto& [name, strct] : it->second.structs) {
                    syms.structs[name] = strct;
                }
                for (auto& [name, enm] : it->second.enums) {
                    syms.enums[name] = enm;
                }
                imports.add(dep_name, std::move(syms));
            }
        }

        ErrorReporter mod_errors;
        auto prog = compile_file(mod.file_path, mod_errors, imports);
        REQUIRE_FALSE(mod_errors.has_errors());
        REQUIRE(prog.has_value());

        // Save artifact
        ModuleArtifact artifact(mod_errors);
        REQUIRE(artifact.save(*prog, mod.qualified_name, build_dir));
        artifact_paths.push_back(build_dir / (mod.qualified_name + ".cmod"));

        compiled[mod.qualified_name] = std::move(*prog);
    }

    // Link all artifacts
    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link(artifact_paths);

    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());

    // Both traits should be in the merged program
    CHECK(merged->traits.count("Position") == 1);
    CHECK(merged->traits.count("LevelData") == 1);

    fs::remove_all(build_dir, ec);
}
