#include "cli/cli_driver.hpp"

#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/module_resolver.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/cpp_entt_codegen.hpp"
#include "cir/cir.hpp"
#include "cir/cir_graphviz.hpp"
#include "cir/cir_json.hpp"
#include "cir/cir_lowering.hpp"
#include "cir/cir_validation.hpp"
#include "cli/cli_options.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace cactus::cli {
namespace {

void print_errors(const cactus::ErrorReporter& errors, std::ostream& err) {
    for (const auto& d : errors.diagnostics()) {
        err << d.location.filename << ":" << d.location.line << ":" << d.location.column << ": "
            << (d.level == cactus::DiagnosticLevel::Error ? "error" : "warning") << ": " << d.message << "\n";
    }
}

/// Read, lex, and parse a source file. Returns nullptr on error.
std::unique_ptr<cactus::ProgramNode> lex_and_parse(const std::string& path, cactus::ErrorReporter& errors) {
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
bool has_use_declarations(const cactus::ProgramNode& prog) {
    return std::ranges::any_of(prog.declarations,
                               [](const auto& decl) { return std::holds_alternative<cactus::UseNode>(decl); });
}

/// Validate the root file's explicit module declaration on CLI paths that may
/// otherwise skip ModuleResolver (for example, single-module builds with no use
/// declarations).
bool validate_root_module_declaration(const std::string& input_file,
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

/// Stamp a copied declaration with its resolved canonical identity, inventing a
/// module-qualified one when semantic analysis left it unset.
template <typename Decl>
Decl export_with_identity(const Decl& decl,
                          cactus::SymbolKind kind,
                          const std::string& module_name,
                          const std::string& name) {
    Decl exported = decl;
    if (!exported.symbol_id.has_value()) {
        exported.symbol_id = cactus::make_symbol_id(kind, module_name, name);
    }
    exported.module_name  = exported.symbol_id->module.name;
    exported.canonical_id = cactus::make_canonical_id(*exported.symbol_id);
    exported.name         = exported.symbol_id->local_name;
    return exported;
}

/// Build ImportedSymbols (pub only) from a compiled DecoratedProgram.
cactus::ImportedSymbols extract_pub_symbols(const std::string& module_name, const cactus::DecoratedProgram& prog) {
    cactus::ImportedSymbols syms;
    syms.module_name = module_name;
    for (const auto& [name, trait] : prog.traits) {
        if (!trait.is_pub) {
            continue;
        }
        syms.traits[name] = export_with_identity(trait, cactus::SymbolKind::Trait, module_name, name);
    }
    for (const auto& [name, strct] : prog.structs) {
        syms.structs[name] = export_with_identity(strct, cactus::SymbolKind::Struct, module_name, name);
    }
    for (const auto& [name, enm] : prog.enums) {
        syms.enums[name] = export_with_identity(enm, cactus::SymbolKind::Enum, module_name, name);
    }
    for (const auto& [name, func] : prog.funcs) {
        if (!func.is_pub) {
            continue;
        }
        syms.funcs[name] = export_with_identity(func, cactus::SymbolKind::Func, module_name, name);
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
        rule.name                 = symbol.local_name;
        rule.module_name          = symbol.module.name;
        rule.canonical_id         = cactus::make_canonical_id(symbol);
        rule.symbol_id            = symbol;
        rule.after_rules          = dep.after_rules;
        syms.rules[dep.rule_name] = rule;
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

bool compile_implicit_std_core(const fs::path& build_dir,
                               const std::vector<fs::path>& search_paths,
                               std::unordered_map<std::string, cactus::DecoratedProgram>& compiled,
                               std::vector<fs::path>& artifact_paths,
                               cactus::ProgramNode& merged_codegen_prog,
                               std::ostream& err) {
    auto std_core_path = cactus::ModuleResolver::locate_file("std.core", search_paths);
    if (std_core_path.empty()) {
        return true;
    }

    cactus::ErrorReporter std_errors;
    auto std_prog = lex_and_parse(std_core_path.string(), std_errors);
    if (!std_prog || std_errors.has_errors()) {
        print_errors(std_errors, err);
        return false;
    }

    cactus::SemanticAnalyzer analyzer(std_errors);
    auto dec = analyzer.analyze(*std_prog);
    if (std_errors.has_errors()) {
        print_errors(std_errors, err);
        return false;
    }

    cactus::ErrorReporter art_errors;
    cactus::ModuleArtifact artifact(art_errors);
    if (!artifact.save(dec, "std.core", build_dir)) {
        print_errors(art_errors, err);
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
bool compile_module(const cactus::ModuleInfo& mod,
                    const fs::path& build_dir,
                    std::unordered_map<std::string, cactus::DecoratedProgram>& compiled,
                    std::vector<fs::path>& artifact_paths,
                    cactus::ProgramNode& merged_codegen_prog,
                    std::ostream& err) {
    // Lex + parse module file
    cactus::ErrorReporter mod_errors;
    auto mod_prog = lex_and_parse(mod.file_path.string(), mod_errors);
    if (!mod_prog || mod_errors.has_errors()) {
        print_errors(mod_errors, err);
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
        print_errors(mod_errors, err);
        return false;
    }

    // Save artifact
    cactus::ErrorReporter art_errors;
    cactus::ModuleArtifact artifact(art_errors);
    if (!artifact.save(dec, mod.qualified_name, build_dir)) {
        print_errors(art_errors, err);
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

/// Runs the multi-module pipeline: resolve, compile each module, link, and
/// restore the merged codegen AST plus root module identity onto `decorated`.
bool link_multi_module_program(const CliOptions& args,
                               const cactus::ProgramNode& root_prog,
                               cactus::ProgramNode& merged_codegen_prog,
                               cactus::DecoratedProgram& decorated,
                               std::ostream& err) {
    // Build directory: sibling of input file, named "build"
    fs::path build_dir = fs::path(args.input_file).parent_path() / "build";
    {
        std::error_code ec;
        fs::create_directories(build_dir, ec);
        if (ec) {
            err << "error: cannot create build directory '" << build_dir.string() << "': " << ec.message() << "\n";
            return false;
        }
    }

    // Prepend stdlib to module search paths
    std::vector<fs::path> all_search_paths;
#ifdef CACTUS_STDLIB_DIR
    all_search_paths.emplace_back(CACTUS_STDLIB_DIR);
#endif
    all_search_paths.insert(all_search_paths.end(), args.module_paths.begin(), args.module_paths.end());

    // Resolve modules in dependency order (leaves first)
    cactus::ErrorReporter resolve_errors;
    cactus::ModuleResolver resolver(resolve_errors);
    auto modules = resolver.resolve(args.input_file, all_search_paths);
    if (resolve_errors.has_errors()) {
        print_errors(resolve_errors, err);
        return false;
    }

    // Compile each module in topo order
    std::unordered_map<std::string, cactus::DecoratedProgram> compiled;
    std::vector<fs::path> artifact_paths;

    // std.core lifecycle events are implicitly in scope for every module.
    // Compile and link it up-front when available so semantic analysis and
    // downstream codegen both see the authoritative declarations.
    if (!compile_implicit_std_core(build_dir, all_search_paths, compiled, artifact_paths, merged_codegen_prog, err)) {
        return false;
    }

    for (auto& mod : modules) {
        if (compiled.contains(mod.qualified_name)) {
            // std.core may appear here from an explicit `use std.core` even
            // though it was already precompiled for lifecycle events.
            // Keep the preloaded artifact as the single source of symbols.
            continue;
        }
        if (!compile_module(mod, build_dir, compiled, artifact_paths, merged_codegen_prog, err)) {
            return false;
        }
    }

    // Link all compiled modules
    cactus::ErrorReporter link_errors;
    cactus::ProgramLinker linker(link_errors);
    auto merged = linker.link(artifact_paths);
    if (!merged || link_errors.has_errors()) {
        print_errors(link_errors, err);
        return false;
    }
    decorated = std::move(*merged);
    // Preserve a merged AST for code generation so imported stdlib/app
    // declarations (units, rules, extern rules, consts, etc.) are
    // still emitted after multi-module linking. Artifacts intentionally do
    // not serialize AST structure.
    decorated.ast = &merged_codegen_prog;
    // The linker produces an empty module_name; restore it from the root
    // module declaration so codegen can emit correctly-namespaced identifiers.
    for (const auto& decl : root_prog.declarations) {
        if (const auto* mod = std::get_if<cactus::ModuleNode>(&decl)) {
            decorated.module_name = mod->name;
            break;
        }
    }
    return true;
}

/// Reformats a written C++ file in place. Never reached for CIR output, whose
/// JSON/DOT/Mermaid text is written verbatim.
void post_process_cpp_file(const std::string& output_file) {
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
#else
    (void)output_file;
#endif
}

/// Writes emitted text to stdout or the requested file. C++-specific
/// post-processing is gated on C++ emission and never sees CIR output.
bool write_output(const CliOptions& args, const std::string& generated, std::ostream& out, std::ostream& err) {
    if (args.output_file.empty()) {
        out << generated;
        return true;
    }

    // CIR is written binary so the serializer's bytes reach the file verbatim;
    // C++ keeps the platform text mode it has always used.
    const auto mode = args.emit == OutputKind::Cir ? std::ios::binary : std::ios::openmode{};
    std::ofstream ofs(args.output_file, std::ios::out | mode);
    if (!ofs.is_open()) {
        err << "error: cannot open output file '" << args.output_file << "'\n";
        return false;
    }
    ofs << generated;
    ofs.close();

    if (args.emit == OutputKind::Cpp) {
        post_process_cpp_file(args.output_file);
        err << "Generated " << args.output_file << " (" << args.backend << " backend)\n";
        return true;
    }
    err << "Generated " << args.output_file << " (CIR v" << cactus::cir::SCHEMA_VERSION << ")\n";
    return true;
}

/// Lowers, validates, and serializes CIR in the format the CLI selected.
std::optional<std::string> emit_cir(const CliOptions& args,
                                    const cactus::DecoratedProgram& decorated,
                                    std::ostream& err) {
    const auto cir = cactus::cir::lower_program(decorated);

    cactus::ErrorReporter cir_errors;
    if (!cactus::cir::validate_program(cir, cir_errors)) {
        print_errors(cir_errors, err);
        return std::nullopt;
    }

    switch (args.cir_format) {
        case CirFormat::Json:
            return cactus::cir::write_json(cir);
        case CirFormat::Dot:
            return cactus::cir::write_dot(cir);
        case CirFormat::Mermaid:
            return cactus::cir::write_mermaid(cir);
    }
    std::unreachable();
}

}  // namespace

int run(int argc, char** argv, std::ostream& out, std::ostream& err) {
    if (argc < 2) {
        print_usage(argv[0], err);
        return 1;
    }

    CliOptions args;
    if (auto exit_code = parse_cli_args(argc, argv, args, err); exit_code.has_value()) {
        return *exit_code;
    }

    // ── Lex + parse root file ─────────────────────────────────────────────────
    cactus::ErrorReporter errors;
    auto root_prog = lex_and_parse(args.input_file, errors);
    if (!root_prog || errors.has_errors()) {
        print_errors(errors, err);
        return 1;
    }
    if (!validate_root_module_declaration(args.input_file, *root_prog, errors) || errors.has_errors()) {
        print_errors(errors, err);
        return 1;
    }

    cactus::DecoratedProgram decorated;
    std::unique_ptr<cactus::ProgramNode> merged_codegen_prog;

    if (has_use_declarations(*root_prog)) {
        merged_codegen_prog           = std::make_unique<cactus::ProgramNode>();
        merged_codegen_prog->location = root_prog->location;
        if (!link_multi_module_program(args, *root_prog, *merged_codegen_prog, decorated, err)) {
            return 1;
        }
    } else {
        // ── Single-module explicit-module pipeline ────────────────────────────
        cactus::SemanticAnalyzer analyzer(errors);
        decorated = analyzer.analyze(*root_prog);
        if (errors.has_errors()) {
            print_errors(errors, err);
            return 1;
        }
    }

    // ── Emission ──────────────────────────────────────────────────────────────
    std::string generated;
    if (args.emit == OutputKind::Cir) {
        auto cir_text = emit_cir(args, decorated, err);
        if (!cir_text.has_value()) {
            return 1;
        }
        generated = std::move(*cir_text);
    } else {
        try {
            generated = cactus::CppEnttCodegen::generate(decorated);
        } catch (const std::exception& e) {
            err << "error: " << e.what() << "\n";
            return 1;
        }
    }

    return write_output(args, generated, out, err) ? 0 : 1;
}

}  // namespace cactus::cli
