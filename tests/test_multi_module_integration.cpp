// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/module_resolver.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/cpp_entt_codegen.hpp"

#include <catch2/catch_test_macros.hpp>

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
    if (errors.has_errors()) {
        return std::nullopt;
    }

    // Parse
    Parser parser(std::move(tokens), errors);
    auto program_node = parser.parse_program();
    if (errors.has_errors()) {
        return std::nullopt;
    }

    // Semantic analyze
    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(program_node, imports);
    if (errors.has_errors()) {
        return std::nullopt;
    }

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
        if (trait.is_pub) {
            player_syms.traits[name] = trait;
        }
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
    auto merged = linker.link({build_dir / "player.cmod", build_dir / "level.cmod"});

    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());
    CHECK(merged->traits.count("Position") == 1);
    CHECK(merged->traits.count("LevelData") == 1);
    CHECK(merged->traits.at("Position").is_pub);
    CHECK(merged->traits.at("LevelData").is_pub);

    fs::remove_all(build_dir, ec);
}

TEST_CASE("integration: module resolver produces topo order for multi_module fixtures", "[integration][7.2]") {
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
        if (modules[i].qualified_name == "player") {
            player_idx = i;
        }
        if (modules[i].qualified_name == "level") {
            level_idx = i;
        }
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
                    if (trait.is_pub) {
                        syms.traits[name] = trait;
                    }
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

// ── std.editor cross-module test (add-std-editor, task 5.4) ────────────────

TEST_CASE("integration: game_templates + editor_module cross-compile with std.editor", "[integration][editor]") {
    auto build_dir = integration_build_dir() / "editor_cross";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Step 1: Compile game_templates.cactus (defines templates and Renderable trait)
    ErrorReporter gt_errors;
    auto gt_prog = compile_file(fixtures_dir() / "game_templates.cactus", gt_errors);
    REQUIRE_FALSE(gt_errors.has_errors());
    REQUIRE(gt_prog.has_value());
    CHECK(gt_prog->traits.count("Renderable") == 1);
    CHECK(gt_prog->pub_templates.count("Box") == 1);
    CHECK(gt_prog->pub_templates.count("PlayerSpawn") == 1);

    // Step 2: Save game_templates artifact
    ModuleArtifact gt_artifact(gt_errors);
    REQUIRE(gt_artifact.save(*gt_prog, "game_templates", build_dir));

    // Step 3: Build imports for editor_module
    ImportedSymbols gt_syms;
    gt_syms.module_name = "game_templates";
    for (auto& [name, trait] : gt_prog->traits) {
        if (trait.is_pub) {
            gt_syms.traits[name] = trait;
        }
    }
    gt_syms.templates = gt_prog->pub_templates;
    ModuleImports editor_imports;
    editor_imports.add("game_templates", std::move(gt_syms));

    // Step 4: Compile editor_module.cactus importing game_templates + std.editor
    ErrorReporter editor_errors;
    auto editor_prog = compile_file(fixtures_dir() / "editor_module.cactus", editor_errors, editor_imports);
    REQUIRE_FALSE(editor_errors.has_errors());
    REQUIRE(editor_prog.has_value());

    // editor_module has locally declared traits (EditorLocked, EditorState) and uses imported Renderable via
    // filter/entity
    CHECK(editor_prog->traits.count("EditorLocked") == 1);
    CHECK(editor_prog->traits.count("EditorState") == 1);

    // Save and link
    ModuleArtifact editor_artifact(editor_errors);
    REQUIRE(editor_artifact.save(*editor_prog, "editor_module", build_dir));

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "game_templates.cmod", build_dir / "editor_module.cmod"});

    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());
    // Merged program should have Renderable (from game_templates) and local traits (from editor_module)
    CHECK(merged->traits.count("Renderable") == 1);
    CHECK(merged->traits.count("EditorLocked") == 1);
    CHECK(merged->traits.count("EditorState") == 1);

    fs::remove_all(build_dir, ec);
}

// ── Editor input ordering (add-editor-camera-navigation, task 1.6) ─────────

/// Lex, parse and semantically analyze source text, keeping the parsed AST
/// alive in program_out for merged-AST code generation.
static std::optional<DecoratedProgram> compile_source(const std::string& source,
                                                      const std::string& filename,
                                                      ProgramNode& program_out,
                                                      ErrorReporter& errors,
                                                      const ModuleImports& imports = {}) {
    Lexer lexer(source, filename, errors);
    auto tokens = lexer.tokenize();
    if (errors.has_errors()) {
        return std::nullopt;
    }
    Parser parser(std::move(tokens), errors);
    program_out = parser.parse_program();
    if (errors.has_errors()) {
        return std::nullopt;
    }
    SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(program_out, imports);
    if (errors.has_errors()) {
        return std::nullopt;
    }
    return decorated;
}

TEST_CASE("integration: stdlib on input handlers run before root-module handlers",
          "[integration][editor][input]") {
    auto build_dir = integration_build_dir() / "input_order";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Step 1: Compile a stdlib-like module with an `on input` system.
    const std::string lib_source = "module editorlib\n"
                                   "\n"
                                   "pub event input\n"
                                   "pub trait EditorNavState:\n"
                                   "    var moves: float\n"
                                   "system EditorNav:\n"
                                   "    filter:\n"
                                   "        EditorNavState\n"
                                   "    on input:\n"
                                   "        moves = moves + 1.0\n";
    ErrorReporter lib_errors;
    ProgramNode lib_ast;
    auto lib_prog = compile_source(lib_source, "editorlib.cactus", lib_ast, lib_errors);
    REQUIRE_FALSE(lib_errors.has_errors());
    REQUIRE(lib_prog.has_value());

    ModuleArtifact lib_artifact(lib_errors);
    REQUIRE(lib_artifact.save(*lib_prog, "editorlib", build_dir));

    // Step 2: Compile a root module with its own `on input` gameplay system.
    ImportedSymbols lib_syms;
    lib_syms.module_name = "editorlib";
    for (auto& [name, trait] : lib_prog->traits) {
        if (trait.is_pub) {
            lib_syms.traits[name] = trait;
        }
    }
    lib_syms.events.insert("input");
    ModuleImports root_imports;
    root_imports.add("editorlib", std::move(lib_syms));

    const std::string root_source = "use editorlib\n"
                                    "\n"
                                    "trait PlayerState:\n"
                                    "    var moves: float\n"
                                    "system GameplayInput:\n"
                                    "    filter:\n"
                                    "        PlayerState\n"
                                    "    on input:\n"
                                    "        moves = moves + 1.0\n";
    ErrorReporter root_errors;
    ProgramNode root_ast;
    auto root_prog = compile_source(root_source, "root.cactus", root_ast, root_errors, root_imports);
    REQUIRE_FALSE(root_errors.has_errors());
    REQUIRE(root_prog.has_value());

    ModuleArtifact root_artifact(root_errors);
    REQUIRE(root_artifact.save(*root_prog, "root", build_dir));

    // Step 3: Link artifacts and rebuild the merged codegen AST the way
    // src/main.cpp does — imported module declarations precede root-module
    // declarations (topological merge order, dependencies first).
    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link({build_dir / "editorlib.cmod", build_dir / "root.cmod"});
    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());

    ProgramNode merged_ast;
    merged_ast.declarations.insert(merged_ast.declarations.end(),
                                   std::make_move_iterator(lib_ast.declarations.begin()),
                                   std::make_move_iterator(lib_ast.declarations.end()));
    merged_ast.declarations.insert(merged_ast.declarations.end(),
                                   std::make_move_iterator(root_ast.declarations.begin()),
                                   std::make_move_iterator(root_ast.declarations.end()));
    merged->ast = &merged_ast;

    const auto code = CppEnttCodegen::generate(*merged);

    // Step 4: The generated update resets consumed input first, then invokes
    // the stdlib module's input handler before the root gameplay handler.
    const auto update_start = code.find("void generated_update_project");
    REQUIRE(update_start != std::string::npos);
    const auto update_end = code.find("\n}\n", update_start);
    REQUIRE(update_end != std::string::npos);
    const auto update = code.substr(update_start, update_end - update_start);

    const auto reset_pos    = update.find("cactus::runtime::entt_backend::reset_consumed_input();");
    const auto editor_pos   = update.find("editor_nav_input(registry, dispatcher, input);");
    const auto gameplay_pos = update.find("gameplay_input_input(registry, dispatcher, input);");
    REQUIRE(reset_pos != std::string::npos);
    REQUIRE(editor_pos != std::string::npos);
    REQUIRE(gameplay_pos != std::string::npos);
    CHECK(reset_pos < editor_pos);
    CHECK(editor_pos < gameplay_pos);

    fs::remove_all(build_dir, ec);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
