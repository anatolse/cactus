// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/module_resolver.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

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
    CHECK(merged->traits.count("player.Position") == 1);
    CHECK(merged->traits.count("level.LevelData") == 1);
    CHECK(merged->traits.at("player.Position").is_pub);
    CHECK(merged->traits.at("level.LevelData").is_pub);

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
    CHECK(merged->traits.count("player.Position") == 1);
    CHECK(merged->traits.count("level.LevelData") == 1);

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
    for (const auto& name : gt_prog->pub_templates) {
        ImportedTemplate tmpl;
        tmpl.name               = name;
        tmpl.canonical_id       = make_canonical_id("game_templates", name);
        gt_syms.templates[name] = tmpl;
    }
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
    CHECK(merged->traits.count("game_templates.Renderable") == 1);
    CHECK(merged->traits.count("editor_module.EditorLocked") == 1);
    CHECK(merged->traits.count("editor_module.EditorState") == 1);

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

TEST_CASE("integration: imported input phase handlers preserve linked declaration order",
          "[integration][phase][input]") {
    auto build_dir = integration_build_dir() / "input_order";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    // Step 1: Compile a dependency that owns the frame root and input phase.
    const std::string lib_source =
        "module editorlib\n"
        "\n"
        "pub extern event frame:\n"
        "    dt: float\n"
        "pub phase input:\n"
        "    from:\n"
        "        frame\n"
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
    const auto input_symbol         = make_symbol_id(SymbolKind::Phase, "editorlib", "input");
    const auto& input_phase         = lib_prog->phases.at("input");
    lib_syms.phase_symbols["input"] = ImportedPhase{.name            = input_symbol.local_name,
                                                    .module_name     = input_symbol.module.name,
                                                    .canonical_id    = make_canonical_id(input_symbol),
                                                    .symbol_id       = input_symbol,
                                                    .fields          = input_phase.fields,
                                                    .upstream_phases = input_phase.upstream_phases,
                                                    .runtime_root    = input_phase.runtime_root,
                                                    .every_seconds   = input_phase.every_seconds,
                                                    .max_repetitions = input_phase.max_repetitions};
    ModuleImports root_imports;
    root_imports.add("editorlib", std::move(lib_syms));

    const std::string root_source =
        "module root\n"
        "use editorlib\n"
        "\n"
        "trait PlayerState:\n"
        "    var moves: float\n"
        "system GameplayInput:\n"
        "    filter:\n"
        "        PlayerState\n"
        "    on editorlib.input:\n"
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

    // Step 4: Graph dispatch uses the canonical phase identity and stable linked
    // declaration order: dependency handler first, root handler second.
    const auto dispatch_start = code.find("void generated_dispatch_phase_editorlib__input(entt::registry& registry");
    REQUIRE(dispatch_start != std::string::npos);
    const auto dispatch_end = code.find("\n}\n", dispatch_start);
    REQUIRE(dispatch_end != std::string::npos);
    const auto dispatch     = code.substr(dispatch_start, dispatch_end - dispatch_start);
    const auto editor_pos   = dispatch.find("editor_nav_input(registry, phase);");
    const auto gameplay_pos = dispatch.find("gameplay_input_editorlib__input(registry, phase);");
    REQUIRE(editor_pos != std::string::npos);
    REQUIRE(gameplay_pos != std::string::npos);
    CHECK(editor_pos < gameplay_pos);

    fs::remove_all(build_dir, ec);
}

// ── Editor glue emission under canonical trait keys ─────────────────────────
// (fix-editor-glue-canonical-traits, D5) Programs linked from artifacts key
// DecoratedProgram.traits by canonical id; editor glue emission must survive
// that keying. Hand-built simple-name-keyed programs would pass regardless.

static fs::path stdlib_dir() {
    return fs::path(CACTUS_TEST_FIXTURES_DIR).parent_path().parent_path() / "stdlib";
}

TEST_CASE("integration: std.core declares canonical external frame and phase graph",
          "[integration][stdlib][phase][7.1]") {
    const std::vector<fs::path> search_paths{stdlib_dir()};
    const auto core_path = ModuleResolver::locate_file("std.core", search_paths);
    REQUIRE_FALSE(core_path.empty());

    ErrorReporter errors;
    const auto core = compile_file(core_path, errors);
    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(core.has_value());

    REQUIRE(core->events.contains("frame"));
    const auto& frame = core->events.at("frame");
    CHECK(frame.is_pub);
    CHECK(frame.is_external);
    REQUIRE(frame.symbol_id.has_value());
    CHECK(*frame.symbol_id == make_symbol_id(SymbolKind::Event, "std.core", "frame"));
    CHECK_FALSE(core->events.contains("input"));
    CHECK_FALSE(core->events.contains("fixed_tick"));
    CHECK_FALSE(core->events.contains("tick"));
    CHECK_FALSE(core->events.contains("late_tick"));

    for (const auto* phase_name : {"input", "fixed_tick", "tick", "late_tick", "render"}) {
        REQUIRE(core->phases.contains(phase_name));
        CHECK(core->phases.at(phase_name).is_pub);
    }
    const auto frame_id = make_symbol_id(SymbolKind::Event, "std.core", "frame");
    REQUIRE(core->phases.at("input").runtime_root.has_value());
    CHECK(*core->phases.at("input").runtime_root == frame_id);
    REQUIRE(core->phases.at("fixed_tick").every_seconds.has_value());
    CHECK(*core->phases.at("fixed_tick").every_seconds > 0.016);
    CHECK(*core->phases.at("fixed_tick").every_seconds < 0.017);
    CHECK(core->phases.at("fixed_tick").max_repetitions == 8);
    REQUIRE(core->phases.at("render").after_phases.size() == 1);
    CHECK(core->phases.at("render").after_phases.front().symbol ==
          make_symbol_id(SymbolKind::Phase, "std.core", "late_tick"));
    CHECK(core->execution_graph.phases.size() == 5);
}

/// Mirrors src/main.cpp extract_pub_symbols: pub symbols (including events)
/// from a live compiled module, for downstream semantic analysis.
static ImportedSymbols pub_symbols_from(const std::string& module_name, const DecoratedProgram& prog) {
    ImportedSymbols syms;
    syms.module_name = module_name;
    for (const auto& [name, trait] : prog.traits) {
        if (!trait.is_pub) {
            continue;
        }
        auto exported = trait;
        if (!exported.symbol_id.has_value()) {
            exported.symbol_id = make_symbol_id(SymbolKind::Trait, module_name, name);
        }
        exported.module_name  = exported.symbol_id->module.name;
        exported.canonical_id = make_canonical_id(*exported.symbol_id);
        exported.name         = exported.symbol_id->local_name;
        syms.traits[name]     = std::move(exported);
    }
    for (const auto& [name, strct] : prog.structs) {
        auto exported = strct;
        if (!exported.symbol_id.has_value()) {
            exported.symbol_id = make_symbol_id(SymbolKind::Struct, module_name, name);
        }
        exported.module_name  = exported.symbol_id->module.name;
        exported.canonical_id = make_canonical_id(*exported.symbol_id);
        exported.name         = exported.symbol_id->local_name;
        syms.structs[name]    = std::move(exported);
    }
    for (const auto& [name, enm] : prog.enums) {
        auto exported = enm;
        if (!exported.symbol_id.has_value()) {
            exported.symbol_id = make_symbol_id(SymbolKind::Enum, module_name, name);
        }
        exported.module_name  = exported.symbol_id->module.name;
        exported.canonical_id = make_canonical_id(*exported.symbol_id);
        exported.name         = exported.symbol_id->local_name;
        syms.enums[name]      = std::move(exported);
    }
    for (const auto& [name, func] : prog.funcs) {
        if (!func.is_pub) {
            continue;
        }
        auto exported = func;
        if (!exported.symbol_id.has_value()) {
            exported.symbol_id = make_symbol_id(SymbolKind::Func, module_name, name);
        }
        exported.module_name  = exported.symbol_id->module.name;
        exported.canonical_id = make_canonical_id(*exported.symbol_id);
        exported.name         = exported.symbol_id->local_name;
        syms.funcs[name]      = std::move(exported);
    }
    for (const auto& name : prog.pub_templates) {
        const auto symbol = make_symbol_id(SymbolKind::Template, module_name, name);
        ImportedTemplate tmpl;
        tmpl.name            = symbol.local_name;
        tmpl.module_name     = symbol.module.name;
        tmpl.canonical_id    = make_canonical_id(symbol);
        tmpl.symbol_id       = symbol;
        syms.templates[name] = tmpl;
    }
    for (const auto& dep : prog.dependency_graph) {
        const auto symbol = make_symbol_id(SymbolKind::System, module_name, dep.system_name);
        ImportedSystem sys;
        sys.name                      = symbol.local_name;
        sys.module_name               = symbol.module.name;
        sys.canonical_id              = make_canonical_id(symbol);
        sys.symbol_id                 = symbol;
        sys.after_systems             = dep.after_systems;
        syms.systems[dep.system_name] = sys;
    }
    for (const auto& [name, event] : prog.events) {
        if (!event.is_pub) {
            continue;
        }
        const auto symbol = event.symbol_id.value_or(make_symbol_id(SymbolKind::Event, module_name, name));
        syms.events.insert(name);
        syms.event_symbols[name] = ImportedEvent{.name         = symbol.local_name,
                                                 .module_name  = symbol.module.name,
                                                 .canonical_id = make_canonical_id(symbol),
                                                 .symbol_id    = symbol,
                                                 .fields       = event.fields,
                                                 .is_external  = event.is_external};
    }
    for (const auto& [name, phase] : prog.phases) {
        if (!phase.is_pub) {
            continue;
        }
        const auto symbol        = phase.symbol_id.value_or(make_symbol_id(SymbolKind::Phase, module_name, name));
        syms.phase_symbols[name] = ImportedPhase{.name            = symbol.local_name,
                                                 .module_name     = symbol.module.name,
                                                 .canonical_id    = make_canonical_id(symbol),
                                                 .symbol_id       = symbol,
                                                 .fields          = phase.fields,
                                                 .upstream_phases = phase.upstream_phases,
                                                 .runtime_root    = phase.runtime_root,
                                                 .every_seconds   = phase.every_seconds,
                                                 .max_repetitions = phase.max_repetitions};
    }
    return syms;
}

/// Mirrors src/main.cpp's multi-module pipeline: resolve against the real
/// stdlib, compile+save each module in topo order, link the artifacts, and
/// attach the merged codegen AST plus root module name.
static std::optional<DecoratedProgram> link_with_stdlib(const std::string& root_source,
                                                        const std::string& root_module,
                                                        const fs::path& build_dir,
                                                        ProgramNode& merged_ast_out) {
    fs::create_directories(build_dir);
    const auto root_file = build_dir / (root_module + ".cactus");
    {
        std::ofstream ofs(root_file);
        ofs << root_source;
    }
    const std::vector<fs::path> search_paths{stdlib_dir()};

    ErrorReporter resolve_errors;
    ModuleResolver resolver(resolve_errors);
    auto modules = resolver.resolve(root_file, search_paths);
    REQUIRE_FALSE(resolve_errors.has_errors());
    REQUIRE_FALSE(modules.empty());

    std::unordered_map<std::string, DecoratedProgram> compiled;
    std::vector<fs::path> artifact_paths;
    std::vector<std::unique_ptr<ProgramNode>> module_asts;

    // std.core runtime declarations are implicitly in scope for every module.
    {
        const auto std_core_path = ModuleResolver::locate_file("std.core", search_paths);
        REQUIRE_FALSE(std_core_path.empty());
        ErrorReporter core_errors;
        auto core_prog = compile_file(std_core_path, core_errors);
        REQUIRE_FALSE(core_errors.has_errors());
        REQUIRE(core_prog.has_value());
        ModuleArtifact core_artifact(core_errors);
        REQUIRE(core_artifact.save(*core_prog, "std.core", build_dir));
        artifact_paths.push_back(build_dir / "std.core.cmod");
        compiled["std.core"] = std::move(*core_prog);
    }

    for (const auto& mod : modules) {
        if (compiled.contains(mod.qualified_name)) {
            continue;
        }
        std::ifstream ifs(mod.file_path);
        REQUIRE(ifs.good());
        std::ostringstream src;
        src << ifs.rdbuf();

        ErrorReporter mod_errors;
        auto ast = std::make_unique<ProgramNode>();
        Lexer lexer(src.str(), mod.file_path.string(), mod_errors);
        auto tokens = lexer.tokenize();
        REQUIRE_FALSE(mod_errors.has_errors());
        Parser parser(std::move(tokens), mod_errors);
        *ast = parser.parse_program();
        REQUIRE_FALSE(mod_errors.has_errors());

        ModuleImports imports;
        if (mod.qualified_name != "std.core") {
            imports.add("std.core",
                        pub_symbols_from("std.core", compiled.at("std.core")),
                        {},
                        compiled.at("std.core").non_pub_templates);
        }
        for (const auto& dep_name : mod.dependencies) {
            auto it = compiled.find(dep_name);
            if (it == compiled.end()) {
                continue;
            }
            std::vector<std::string> qualifiers;
            for (const auto& decl : ast->declarations) {
                const auto* use = std::get_if<UseNode>(&decl);
                if (use != nullptr && use->module_name == dep_name) {
                    qualifiers.push_back(use->alias.value_or(use->module_name));
                }
            }
            if (qualifiers.empty()) {
                qualifiers.push_back(dep_name);
            }
            for (const auto& qualifier : qualifiers) {
                imports.add(qualifier, pub_symbols_from(dep_name, it->second), {}, it->second.non_pub_templates);
            }
        }

        SemanticAnalyzer analyzer(mod_errors);
        auto dec = analyzer.analyze(*ast, imports);
        if (mod_errors.has_errors()) {
            for (const auto& diag : mod_errors.diagnostics()) {
                UNSCOPED_INFO(mod.qualified_name << ": " << diag.message);
            }
        }
        REQUIRE_FALSE(mod_errors.has_errors());

        ModuleArtifact artifact(mod_errors);
        REQUIRE(artifact.save(dec, mod.qualified_name, build_dir));
        artifact_paths.push_back(build_dir / (mod.qualified_name + ".cmod"));

        merged_ast_out.declarations.insert(merged_ast_out.declarations.end(),
                                           std::make_move_iterator(ast->declarations.begin()),
                                           std::make_move_iterator(ast->declarations.end()));
        module_asts.push_back(std::move(ast));
        compiled[mod.qualified_name] = std::move(dec);
    }

    ErrorReporter link_errors;
    ProgramLinker linker(link_errors);
    auto merged = linker.link(artifact_paths);
    REQUIRE_FALSE(link_errors.has_errors());
    REQUIRE(merged.has_value());
    merged->ast         = &merged_ast_out;
    merged->module_name = root_module;
    return merged;
}

TEST_CASE("integration: linked 2D editor program emits hit-test/spawn glue and 2D rig",
          "[integration][editor][canonical]") {
    auto build_dir = integration_build_dir() / "editor_glue_2d";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    const std::string source =
        "module editor2d_app\n"
        "use std.transform.flat as tf\n"
        "use std.physics.flat as phys\n"
        "use std.camera.flat as cam2d\n"
        "use std.camera.viewport as vp\n"
        "use std.editor as editor\n"
        "\n"
        "pub template Crate:\n"
        "    tf.WorldTransform\n"
        "    phys.BoxCollider:\n"
        "        size = vec2(1.0, 1.0)\n"
        "\n"
        "pub entity Cam:\n"
        "    cam2d.Camera:\n"
        "        zoom = 32.0\n"
        "    vp.Viewport\n"
        "\n"
        "pub entity Crate1:\n"
        "    tf.WorldTransform\n"
        "    phys.BoxCollider:\n"
        "        size = vec2(1.0, 1.0)\n";

    ProgramNode merged_ast;
    auto merged = link_with_stdlib(source, "editor2d_app", build_dir, merged_ast);
    REQUIRE(merged.has_value());
    // Linked programs key traits canonically — the shape this change fixes.
    REQUIRE(merged->traits.contains("std.transform.flat.WorldTransform"));
    REQUIRE(merged->traits.contains("std.transform.volume.WorldTransform"));

    const auto code = CppEnttCodegen::generate(*merged);

    // Hit-test and spawn impls are registered with resolved component names.
    CHECK(code.find("register_editor_hit_test_impl") != std::string::npos);
    CHECK(code.find("register_editor_spawn_impl") != std::string::npos);
    CHECK(code.find("reg.view<std_transform_flat__WorldTransform, std_physics_flat__BoxCollider>") !=
          std::string::npos);
    CHECK(code.find("wt->position = pos2d") != std::string::npos);

    // 2D rig branch and viewport camera helper.
    CHECK(code.find("EditorCamera2D{.view_center") != std::string::npos);
    CHECK(code.find("set_active_camera_2d") != std::string::npos);

    // Edit-mode HUD overlay gated on the resolved EditorState component.
    CHECK(code.find("registry.view<std_editor__EditorState>()") != std::string::npos);
    CHECK(code.find("EDIT [") != std::string::npos);

    // No 3D glue for a flat-only root program.
    CHECK(code.find("register_editor_raycast_impl") == std::string::npos);
    CHECK(code.find("set_active_camera_3d") == std::string::npos);

    // Dimensionality is deterministic across runs.
    CHECK(code == CppEnttCodegen::generate(*merged));

    fs::remove_all(build_dir, ec);
}

TEST_CASE("integration: linked 3D editor program emits raycast glue and 3D rig", "[integration][editor][canonical]") {
    auto build_dir = integration_build_dir() / "editor_glue_3d";
    std::error_code ec;
    fs::remove_all(build_dir, ec);

    const std::string source =
        "module editor3d_app\n"
        "use std.transform.volume as tv\n"
        "use std.render.models as models\n"
        "use std.camera.volume as cam3d\n"
        "use std.camera.viewport as vp\n"
        "use std.editor as editor\n"
        "\n"
        "asset Bot: model = \"bot.glb\"\n"
        "\n"
        "pub template Robot:\n"
        "    tv.WorldTransform\n"
        "    models.ModelRenderer:\n"
        "        model = Bot\n"
        "        visible = true\n"
        "\n"
        "pub entity MainCamera:\n"
        "    tv.WorldTransform\n"
        "    cam3d.Camera:\n"
        "        fov_y = 60.0\n"
        "    vp.Viewport\n";

    ProgramNode merged_ast;
    auto merged = link_with_stdlib(source, "editor3d_app", build_dir, merged_ast);
    REQUIRE(merged.has_value());

    const auto code = CppEnttCodegen::generate(*merged);

    // Raycast and spawn impls with resolved volume component names.
    CHECK(code.find("register_editor_raycast_impl") != std::string::npos);
    CHECK(code.find("register_editor_spawn_impl") != std::string::npos);
    CHECK(code.find("reg.view<std_transform_volume__WorldTransform, std_render_models__ModelRenderer>") !=
          std::string::npos);
    CHECK(code.find("wt->position = pos3d") != std::string::npos);

    // 3D rig branch of camera_enter and the 3D viewport camera helper.
    CHECK(code.find("if (use_3d)") != std::string::npos);
    CHECK(code.find("EditorCamera3D{.focus") != std::string::npos);
    CHECK(code.find("set_active_camera_3d") != std::string::npos);

    // No 2D glue for a volume-only root program.
    CHECK(code.find("register_editor_hit_test_impl") == std::string::npos);
    CHECK(code.find("set_active_camera_2d") == std::string::npos);
    CHECK(code.find("EditorCamera2D{.view_center") == std::string::npos);

    // Dimensionality is deterministic across runs.
    CHECK(code == CppEnttCodegen::generate(*merged));

    fs::remove_all(build_dir, ec);
}

TEST_CASE("integration: proposal frame graph and contracted handlers lower end to end",
          "[integration][runtime-phases][handler-contract][8.2]") {
    ErrorReporter errors;
    const auto fixture_path = fs::path(CACTUS_TEST_FIXTURES_DIR) / "runtime_phase_contracts.cactus";
    std::ifstream fixture(fixture_path);
    REQUIRE(fixture.good());
    std::ostringstream fixture_source;
    fixture_source << fixture.rdbuf();
    ProgramNode fixture_ast;
    auto decorated = compile_source(fixture_source.str(), fixture_path.string(), fixture_ast, errors);

    REQUIRE_FALSE(errors.has_errors());
    REQUIRE(decorated.has_value());
    REQUIRE(decorated->execution_graph.phases.size() == 5);
    REQUIRE(decorated->execution_graph.handlers.size() == 4);

    const auto handler = [](const std::string& system, HandlerTriggerKind kind, const std::string& trigger) {
        return HandlerIdentity{
            .system  = make_symbol_id(SymbolKind::System, "runtime_phase_contracts", system),
            .trigger = ResolvedHandlerTrigger{
                .kind   = kind,
                .symbol = make_symbol_id(kind == HandlerTriggerKind::Phase ? SymbolKind::Phase : SymbolKind::Event,
                                         "runtime_phase_contracts",
                                         trigger)}};
    };
    const auto find_handler = [&](const HandlerIdentity& identity) -> const HandlerNode* {
        const auto it = std::ranges::find_if(decorated->execution_graph.handlers,
                                             [&](const HandlerNode& node) { return node.identity == identity; });
        return it == decorated->execution_graph.handlers.end() ? nullptr : &*it;
    };

    const auto* input = find_handler(handler("InputSource", HandlerTriggerKind::Phase, "input"));
    REQUIRE(input != nullptr);
    CHECK(input->contract.is_selectionless);
    CHECK(input->contract.emits.size() == 1);
    CHECK(input->contract.effects == std::unordered_set<std::string>{"input"});

    const auto* movement = find_handler(handler("NativeMovement", HandlerTriggerKind::Phase, "fixed_tick"));
    REQUIRE(movement != nullptr);
    CHECK_FALSE(movement->contract.is_selectionless);
    CHECK(movement->contract.reads ==
          std::unordered_set<SymbolId>{make_symbol_id(SymbolKind::Trait, "runtime_phase_contracts", "Position"),
                                       make_symbol_id(SymbolKind::Trait, "runtime_phase_contracts", "Velocity")});
    CHECK(movement->contract.writes.size() == 1);
    CHECK(movement->contract.emits.size() == 1);
    CHECK(movement->contract.commands.size() == 1);
    CHECK(movement->contract.effects == std::unordered_set<std::string>{"physics"});

    const auto* renderer = find_handler(handler("SpriteRenderer", HandlerTriggerKind::Phase, "render"));
    REQUIRE(renderer != nullptr);
    CHECK(renderer->contract.reads.size() == 2);
    CHECK(renderer->contract.effects == std::unordered_set<std::string>{"graphics"});

    const auto* audio = find_handler(handler("AudioOutput", HandlerTriggerKind::Event, "SoundRequested"));
    REQUIRE(audio != nullptr);
    CHECK(audio->contract.is_selectionless);
    CHECK(audio->contract.effects == std::unordered_set<std::string>{"audio"});

    const auto code = CppEnttCodegen::generate(*decorated);
    CHECK(code.find("generated_inject_external_event(runtime_phase_contracts__frameEvent occurrence)") !=
          std::string::npos);
    CHECK(code.find("NativeMovement__on__runtime_phase_contracts__fixed_tick") != std::string::npos);
    CHECK(code.find("SpriteRenderer__on__runtime_phase_contracts__render") != std::string::npos);
    CHECK(code.find("InputSource__on__runtime_phase_contracts__input") != std::string::npos);
    CHECK(code.find("AudioOutput__on__runtime_phase_contracts__SoundRequested") != std::string::npos);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
