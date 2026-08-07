#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/module_resolver.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/cpp_entt_codegen.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ── Helpers ──────────────────────────────────────────────────────────────────

static void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <input.cactus> [options]\n"
              << "\nOptions:\n"
              << "  --backend <cpp-entt>                Code generation backend (default: cpp-entt)\n"
              << "  --output <file>                      Output file (default: stdout)\n"
              << "  --module-path <dir>                  Additional module search directory (repeatable)\n"
              << "  --help                               Show this help message\n";
}

static void print_errors(const cactus::ErrorReporter& errors) {
    for (const auto& d : errors.diagnostics()) {
        std::cerr << d.location.filename << ":" << d.location.line << ":" << d.location.column << ": "
                  << (d.level == cactus::DiagnosticLevel::Error ? "error" : "warning") << ": " << d.message << "\n";
    }
}

/// Read, lex, and parse a source file. Returns nullptr on error.
static std::unique_ptr<cactus::ProgramNode> lex_and_parse(const std::string& path, cactus::ErrorReporter& errors) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        errors.error({}, "cannot open file '" + path + "'");
        return nullptr;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string source = ss.str();

    cactus::Lexer lexer(source, path, errors);
    auto tokens = lexer.tokenize();
    if (errors.has_errors()) {
        return nullptr;
    }

    cactus::Parser parser(std::move(tokens), errors);
    auto prog = std::make_unique<cactus::ProgramNode>(parser.parse_program());
    if (errors.has_errors()) {
        return nullptr;
    }
    return prog;
}

/// Returns true if program has any `use` declarations.
static bool has_use_declarations(const cactus::ProgramNode& prog) {
    return std::ranges::any_of(
        prog.declarations, [](const auto& decl) { return std::holds_alternative<cactus::UseNode>(decl); });
}

/// Validate the root file's explicit module declaration on CLI paths that may
/// otherwise skip ModuleResolver (for example, single-module builds with no use
/// declarations).
static bool validate_root_module_declaration(const std::string& input_file,
                                             const cactus::ProgramNode& root_prog,
                                             cactus::ErrorReporter& errors) {
    std::error_code ec;
    const auto canonical_input = fs::canonical(input_file, ec);
    if (ec) {
        errors.error({}, "cannot resolve input file '" + input_file + "': " + ec.message());
        return false;
    }

    const auto inferred_name =
        cactus::ModuleResolver::infer_module_name(canonical_input.parent_path(), canonical_input);
    cactus::ModuleResolver validator(errors);
    return validator.validate_module_name(root_prog, inferred_name, canonical_input);
}

/// Build ImportedSymbols (pub only) from a compiled DecoratedProgram.
static cactus::ImportedSymbols extract_pub_symbols(const std::string& module_name,
                                                   const cactus::DecoratedProgram& prog) {
    cactus::ImportedSymbols syms;
    syms.module_name = module_name;
    for (const auto& [name, trait] : prog.traits) {
        if (trait.is_pub) {
            auto exported = trait;
            if (!exported.symbol_id.has_value()) {
                exported.symbol_id = cactus::make_symbol_id(cactus::SymbolKind::Trait, module_name, name);
            }
            exported.module_name  = exported.symbol_id->module.name;
            exported.canonical_id = cactus::make_canonical_id(*exported.symbol_id);
            exported.name         = exported.symbol_id->local_name;
            syms.traits[name]     = std::move(exported);
        }
    }
    for (const auto& [name, strct] : prog.structs) {
        auto exported = strct;
        if (!exported.symbol_id.has_value()) {
            exported.symbol_id = cactus::make_symbol_id(cactus::SymbolKind::Struct, module_name, name);
        }
        exported.module_name  = exported.symbol_id->module.name;
        exported.canonical_id = cactus::make_canonical_id(*exported.symbol_id);
        exported.name         = exported.symbol_id->local_name;
        syms.structs[name]    = std::move(exported);
    }
    for (const auto& [name, enm] : prog.enums) {
        auto exported = enm;
        if (!exported.symbol_id.has_value()) {
            exported.symbol_id = cactus::make_symbol_id(cactus::SymbolKind::Enum, module_name, name);
        }
        exported.module_name  = exported.symbol_id->module.name;
        exported.canonical_id = cactus::make_canonical_id(*exported.symbol_id);
        exported.name         = exported.symbol_id->local_name;
        syms.enums[name]      = std::move(exported);
    }
    for (const auto& [name, func] : prog.funcs) {
        if (func.is_pub) {
            auto exported = func;
            if (!exported.symbol_id.has_value()) {
                exported.symbol_id = cactus::make_symbol_id(cactus::SymbolKind::Func, module_name, name);
            }
            exported.module_name  = exported.symbol_id->module.name;
            exported.canonical_id = cactus::make_canonical_id(*exported.symbol_id);
            exported.name         = exported.symbol_id->local_name;
            syms.funcs[name]      = std::move(exported);
        }
    }
    for (const auto& name : prog.pub_templates) {
        const auto symbol = cactus::make_symbol_id(cactus::SymbolKind::Template, module_name, name);
        cactus::ImportedTemplate tmpl;
        tmpl.name            = symbol.local_name;
        tmpl.module_name     = symbol.module.name;
        tmpl.canonical_id    = cactus::make_canonical_id(symbol);
        tmpl.symbol_id       = symbol;
        syms.templates[name] = tmpl;
    }
    for (const auto& dep : prog.dependency_graph) {
        const auto symbol = cactus::make_symbol_id(cactus::SymbolKind::Rule, module_name, dep.rule_name);
        cactus::ImportedRule rule;
        rule.name                   = symbol.local_name;
        rule.module_name            = symbol.module.name;
        rule.canonical_id           = cactus::make_canonical_id(symbol);
        rule.symbol_id              = symbol;
        rule.after_rules            = dep.after_rules;
        syms.rules[dep.rule_name]   = rule;
    }
    // Export public runtime declarations with their typed canonical identity
    // and external-event provenance intact.
    for (const auto& [name, event] : prog.events) {
        if (!event.is_pub) {
            continue;
        }
        const auto symbol =
            event.symbol_id.value_or(cactus::make_symbol_id(cactus::SymbolKind::Event, module_name, name));
        syms.events.insert(name);
        syms.event_symbols[name] = cactus::ImportedEvent{.name         = symbol.local_name,
                                                         .module_name  = symbol.module.name,
                                                         .canonical_id = cactus::make_canonical_id(symbol),
                                                         .symbol_id    = symbol,
                                                         .fields       = event.fields,
                                                         .is_external  = event.is_external};
    }
    for (const auto& [name, phase] : prog.phases) {
        if (!phase.is_pub) {
            continue;
        }
        const auto symbol =
            phase.symbol_id.value_or(cactus::make_symbol_id(cactus::SymbolKind::Phase, module_name, name));
        syms.phase_symbols[name] = cactus::ImportedPhase{.name            = symbol.local_name,
                                                         .module_name     = symbol.module.name,
                                                         .canonical_id    = cactus::make_canonical_id(symbol),
                                                         .symbol_id       = symbol,
                                                         .fields          = phase.fields,
                                                         .upstream_phases = phase.upstream_phases,
                                                         .runtime_root    = phase.runtime_root,
                                                         .every_seconds   = phase.every_seconds,
                                                         .max_repetitions = phase.max_repetitions};
    }
    return syms;
}

static bool compile_implicit_std_core(const fs::path& build_dir,
                                      const std::vector<fs::path>& search_paths,
                                      std::unordered_map<std::string, cactus::DecoratedProgram>& compiled,
                                      std::vector<fs::path>& artifact_paths,
                                      cactus::ProgramNode& merged_codegen_prog) {
    auto std_core_path = cactus::ModuleResolver::locate_file("std.core", search_paths);
    if (std_core_path.empty()) {
        return true;
    }

    cactus::ErrorReporter std_errors;
    auto std_prog = lex_and_parse(std_core_path.string(), std_errors);
    if (!std_prog || std_errors.has_errors()) {
        print_errors(std_errors);
        return false;
    }

    cactus::SemanticAnalyzer analyzer(std_errors);
    auto dec = analyzer.analyze(*std_prog);
    if (std_errors.has_errors()) {
        print_errors(std_errors);
        return false;
    }

    cactus::ErrorReporter art_errors;
    cactus::ModuleArtifact artifact(art_errors);
    if (!artifact.save(dec, "std.core", build_dir)) {
        print_errors(art_errors);
        return false;
    }

    artifact_paths.push_back(build_dir / "std.core.cmod");
    compiled["std.core"] = std::move(dec);

    // Preserve std.core's declaration ASTs for final codegen so its runtime
    // events, phases, traits, and rules (frame, phase graph, SceneCleanup,
    // etc.) are emitted even though it is precompiled and skipped in the
    // per-module merge loop below. Tag events with their source module so
    // emit_event uses the canonical module prefix.
    for (auto& decl : std_prog->declarations) {
        if (auto* ev = std::get_if<cactus::EventNode>(&decl)) {
            ev->module_name = "std.core";
        }
    }
    merged_codegen_prog.declarations.insert(merged_codegen_prog.declarations.end(),
                                            std::make_move_iterator(std_prog->declarations.begin()),
                                            std::make_move_iterator(std_prog->declarations.end()));
    return true;
}

/// Compiles one resolved module (lex, parse, semantic-analyze, save artifact,
/// merge into the codegen AST), updating `compiled`/`artifact_paths`/
/// `merged_codegen_prog` on success. Prints diagnostics and returns false on
/// any failure.
static bool compile_module(const cactus::ModuleInfo& mod,
                           const fs::path& build_dir,
                           std::unordered_map<std::string, cactus::DecoratedProgram>& compiled,
                           std::vector<fs::path>& artifact_paths,
                           cactus::ProgramNode& merged_codegen_prog) {
    // Lex + parse module file
    cactus::ErrorReporter mod_errors;
    auto mod_prog = lex_and_parse(mod.file_path.string(), mod_errors);
    if (!mod_prog || mod_errors.has_errors()) {
        print_errors(mod_errors);
        return false;
    }

    // Build ModuleImports from already-compiled dependencies.
    // Register each dependency under the same qualifier the source uses:
    // either the module name itself, or the `use ... as alias` alias.
    cactus::ModuleImports imports;
    auto std_core_it = compiled.find("std.core");
    if (std_core_it != compiled.end() && mod.qualified_name != "std.core") {
        auto syms = extract_pub_symbols("std.core", std_core_it->second);
        imports.add("std.core", std::move(syms), {}, std_core_it->second.non_pub_templates);
    }
    auto qualifiers_for_dependency = [&mod_prog](const std::string& dep_name) {
        std::vector<std::string> qualifiers;
        for (const auto& decl : mod_prog->declarations) {
            const auto* use = std::get_if<cactus::UseNode>(&decl);
            if (use != nullptr && use->module_name == dep_name) {
                qualifiers.push_back(use->alias.value_or(use->module_name));
            }
        }
        if (qualifiers.empty()) {
            qualifiers.push_back(dep_name);
        }
        return qualifiers;
    };
    for (const auto& dep_name : mod.dependencies) {
        auto it = compiled.find(dep_name);
        if (it == compiled.end()) {
            continue;
        }
        for (const auto& qualifier : qualifiers_for_dependency(dep_name)) {
            auto syms = extract_pub_symbols(dep_name, it->second);
            imports.add(qualifier, std::move(syms), {}, it->second.non_pub_templates);
        }
    }

    // Semantic analyze
    cactus::SemanticAnalyzer analyzer(mod_errors);
    auto dec = analyzer.analyze(*mod_prog, imports);
    if (mod_errors.has_errors()) {
        print_errors(mod_errors);
        return false;
    }

    // Save artifact
    cactus::ErrorReporter art_errors;
    cactus::ModuleArtifact artifact(art_errors);
    if (!artifact.save(dec, mod.qualified_name, build_dir)) {
        print_errors(art_errors);
        return false;
    }
    artifact_paths.push_back(build_dir / (mod.qualified_name + ".cmod"));

    // Preserve full declaration ASTs for final codegen so imported
    // units/rules/extern rules from stdlib modules are also emitted.
    // Tag events with their source module so emit_event can use the canonical module prefix.
    for (auto& decl : mod_prog->declarations) {
        if (auto* ev = std::get_if<cactus::EventNode>(&decl)) {
            ev->module_name = mod.qualified_name;
        }
    }
    merged_codegen_prog.declarations.insert(merged_codegen_prog.declarations.end(),
                                            std::make_move_iterator(mod_prog->declarations.begin()),
                                            std::make_move_iterator(mod_prog->declarations.end()));

    compiled[mod.qualified_name] = std::move(dec);
    return true;
}

/// Parsed CLI arguments (see print_usage for the flag reference).
struct CliArgs {
    std::string input_file;
    std::string backend = "cpp-entt";
    std::string output_file;
    std::vector<fs::path> module_paths;  // 6.1: --module-path flag
};

/// Parses argv into `out`. Returns an exit code if main() should return
/// immediately (0 for --help, 1 for a usage error), or nullopt to continue.
static std::optional<int> parse_cli_args(int argc, char** argv, CliArgs& out) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--backend") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "error: --backend requires an argument\n";
                return 1;
            }
            out.backend = argv[++i];
            if (out.backend != "cpp-entt") {
                std::cerr << "error: unknown backend '" << out.backend << "' (use cpp-entt)\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "error: --output requires an argument\n";
                return 1;
            }
            out.output_file = argv[++i];
        } else if (std::strcmp(argv[i], "--module-path") == 0) {
            // 6.1: collect repeatable --module-path directories
            if (i + 1 >= argc) {
                std::cerr << "error: --module-path requires an argument\n";
                return 1;
            }
            out.module_paths.emplace_back(argv[++i]);
        } else if (argv[i][0] == '-') {
            std::cerr << "error: unknown option '" << argv[i] << "'\n";
            return 1;
        } else {
            out.input_file = argv[i];
        }
    }

    if (out.input_file.empty()) {
        std::cerr << "error: no input file specified\n";
        return 1;
    }
    return std::nullopt;
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {  // NOLINT(readability-function-cognitive-complexity) -- still 43 after parse_cli_args/compile_module extraction; remaining branching is the multi-module vs single-module pipeline split plus codegen/output, not further in scope here
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    CliArgs args;
    if (auto exit_code = parse_cli_args(argc, argv, args); exit_code.has_value()) {
        return *exit_code;
    }
    const std::string& input_file             = args.input_file;
    std::string backend                       = args.backend;
    const std::string& output_file            = args.output_file;
    const std::vector<fs::path>& module_paths = args.module_paths;

    // ── Lex + parse root file ─────────────────────────────────────────────────
    cactus::ErrorReporter errors;
    auto root_prog = lex_and_parse(input_file, errors);
    if (!root_prog || errors.has_errors()) {
        print_errors(errors);
        return 1;
    }
    if (!validate_root_module_declaration(input_file, *root_prog, errors) || errors.has_errors()) {
        print_errors(errors);
        return 1;
    }

    cactus::DecoratedProgram decorated;
    std::unique_ptr<cactus::ProgramNode> merged_codegen_prog;

    // ── 6.2: Multi-module pipeline ───────────────────────────────────────────
    if (has_use_declarations(*root_prog)) {
        merged_codegen_prog           = std::make_unique<cactus::ProgramNode>();
        merged_codegen_prog->location = root_prog->location;

        // Build directory: sibling of input file, named "build"
        fs::path build_dir = fs::path(input_file).parent_path() / "build";
        {
            std::error_code ec;
            fs::create_directories(build_dir, ec);
            if (ec) {
                std::cerr << "error: cannot create build directory '" << build_dir.string() << "': " << ec.message()
                          << "\n";
                return 1;
            }
        }

        // Prepend stdlib to module search paths (task 9.2)
        std::vector<fs::path> all_search_paths;
#ifdef CACTUS_STDLIB_DIR
        all_search_paths.emplace_back(CACTUS_STDLIB_DIR);
#endif
        all_search_paths.insert(all_search_paths.end(), module_paths.begin(), module_paths.end());

        // Resolve modules in dependency order (leaves first)
        cactus::ErrorReporter resolve_errors;
        cactus::ModuleResolver resolver(resolve_errors);
        auto modules = resolver.resolve(input_file, all_search_paths);
        if (resolve_errors.has_errors()) {
            print_errors(resolve_errors);
            return 1;
        }

        // Compile each module in topo order
        std::unordered_map<std::string, cactus::DecoratedProgram> compiled;
        std::vector<fs::path> artifact_paths;

        // std.core lifecycle events are implicitly in scope for every module.
        // Compile and link it up-front when available so semantic analysis and
        // downstream codegen both see the authoritative declarations.
        if (!compile_implicit_std_core(build_dir, all_search_paths, compiled, artifact_paths, *merged_codegen_prog)) {
            return 1;
        }

        for (auto& mod : modules) {
            if (compiled.contains(mod.qualified_name)) {
                // std.core may appear here from an explicit `use std.core` even
                // though it was already precompiled for lifecycle events.
                // Keep the preloaded artifact as the single source of symbols.
                continue;
            }
            if (!compile_module(mod, build_dir, compiled, artifact_paths, *merged_codegen_prog)) {
                return 1;
            }
        }

        // Link all compiled modules
        cactus::ErrorReporter link_errors;
        cactus::ProgramLinker linker(link_errors);
        auto merged = linker.link(artifact_paths);
        if (!merged || link_errors.has_errors()) {
            print_errors(link_errors);
            return 1;
        }
        decorated = std::move(*merged);
        // Preserve a merged AST for code generation so imported stdlib/app
        // declarations (units, rules, extern rules, consts, etc.) are
        // still emitted after multi-module linking. Artifacts intentionally do
        // not serialize AST structure.
        decorated.ast = merged_codegen_prog.get();
        // The linker produces an empty module_name; restore it from the root
        // module declaration so codegen can emit correctly-namespaced identifiers.
        for (const auto& decl : root_prog->declarations) {
            if (const auto* mod = std::get_if<cactus::ModuleNode>(&decl)) {
                decorated.module_name = mod->name;
                break;
            }
        }

    } else {
        // ── Single-module explicit-module pipeline ─────────────────────────────
        cactus::SemanticAnalyzer analyzer(errors);
        decorated = analyzer.analyze(*root_prog);
        if (errors.has_errors()) {
            print_errors(errors);
            return 1;
        }
    }

    // ── Code generation ───────────────────────────────────────────────────────
    std::string generated;
    try {
        generated = cactus::CppEnttCodegen::generate(decorated);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    // ── Output ────────────────────────────────────────────────────────────────
    if (output_file.empty()) {
        std::cout << generated;
    } else {
        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            std::cerr << "error: cannot open output file '" << output_file << "'\n";
            return 1;
        }
        ofs << generated;
        ofs.close();

#ifdef CACTUS_CLANG_FORMAT_EXE_PATH
        if (std::strlen(CACTUS_CLANG_FORMAT_EXE_PATH) > 0) {
            std::string clang_format_path;
            if (!clang_format_path.empty() && clang_format_path.front() == '"' && clang_format_path.back() == '"') {
                clang_format_path = clang_format_path.substr(1, clang_format_path.size() - 2);
            }

            const std::string format_command = std::string{"\""} + clang_format_path + "\" -i \"" + output_file + "\"";
            // NOLINTNEXTLINE(bugprone-command-processor)
            (void)std::system(format_command.c_str());
        }
#endif

        std::cerr << "Generated " << output_file << " (" << backend << " backend)\n";
    }

    return 0;
}
