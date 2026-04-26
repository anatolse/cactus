#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/module_resolver.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/cpp_entt_codegen.hpp"
#include "backends/cpp-manual/cpp_manual_codegen.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ── Helpers ──────────────────────────────────────────────────────────────────

static void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <input.cactus> [options]\n"
              << "\nOptions:\n"
              << "  --backend <cpp-manual|cpp-entt>     Code generation backend (default: cpp-entt)\n"
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
    for (const auto& decl : prog.declarations) {  // NOLINT(readability-use-anyofallof)
        if (std::holds_alternative<cactus::UseNode>(decl)) {
            return true;
        }
    }
    return false;
}

/// Build ImportedSymbols (pub only) from a compiled DecoratedProgram.
static cactus::ImportedSymbols extract_pub_symbols(const std::string& module_name,
                                                   const cactus::DecoratedProgram& prog) {
    cactus::ImportedSymbols syms;
    syms.module_name = module_name;
    for (const auto& [name, trait] : prog.traits) {
        if (trait.is_pub) {
            syms.traits[name] = trait;
        }
    }
    for (const auto& [name, strct] : prog.structs) {
        syms.structs[name] = strct;
    }
    for (const auto& [name, enm] : prog.enums) {
        syms.enums[name] = enm;
    }
    for (const auto& [name, func] : prog.funcs) {
        if (func.is_pub) {
            syms.funcs[name] = func;
        }
    }
    // Export pub event names so downstream modules can validate handlers
    syms.events = prog.pub_events;
    return syms;
}

static bool compile_implicit_std_core(const fs::path& build_dir,
                                      const std::vector<fs::path>& search_paths,
                                      std::unordered_map<std::string, cactus::DecoratedProgram>& compiled,
                                      std::vector<fs::path>& artifact_paths) {
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
    return true;
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {  // NOLINT(readability-function-cognitive-complexity)
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_file;
    std::string backend = "cpp-entt";
    std::string output_file;
    std::vector<fs::path> module_paths;  // 6.1: --module-path flag

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
            backend = argv[++i];
            if (backend != "cpp-manual" && backend != "cpp-entt") {
                std::cerr << "error: unknown backend '" << backend << "' (use cpp-manual or cpp-entt)\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "error: --output requires an argument\n";
                return 1;
            }
            output_file = argv[++i];
        } else if (std::strcmp(argv[i], "--module-path") == 0) {
            // 6.1: collect repeatable --module-path directories
            if (i + 1 >= argc) {
                std::cerr << "error: --module-path requires an argument\n";
                return 1;
            }
            module_paths.emplace_back(argv[++i]);
        } else if (argv[i][0] == '-') {
            std::cerr << "error: unknown option '" << argv[i] << "'\n";
            return 1;
        } else {
            input_file = argv[i];
        }
    }

    if (input_file.empty()) {
        std::cerr << "error: no input file specified\n";
        return 1;
    }

    // ── Lex + parse root file ─────────────────────────────────────────────────
    cactus::ErrorReporter errors;
    auto root_prog = lex_and_parse(input_file, errors);
    if (!root_prog || errors.has_errors()) {
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
        if (!compile_implicit_std_core(build_dir, all_search_paths, compiled, artifact_paths)) {
            return 1;
        }

        for (auto& mod : modules) {
            // Build ModuleImports from already-compiled dependencies
            cactus::ModuleImports imports;
            auto std_core_it = compiled.find("std.core");
            if (std_core_it != compiled.end() && mod.qualified_name != "std.core") {
                auto syms = extract_pub_symbols("std.core", std_core_it->second);
                imports.add("std.core", std::move(syms));
            }
            for (auto& dep_name : mod.dependencies) {
                auto it = compiled.find(dep_name);
                if (it != compiled.end()) {
                    auto syms = extract_pub_symbols(dep_name, it->second);
                    imports.add(dep_name, std::move(syms));
                }
            }

            // Lex + parse module file
            cactus::ErrorReporter mod_errors;
            auto mod_prog = lex_and_parse(mod.file_path.string(), mod_errors);
            if (!mod_prog || mod_errors.has_errors()) {
                print_errors(mod_errors);
                return 1;
            }

            // Semantic analyze
            cactus::SemanticAnalyzer analyzer(mod_errors);
            auto dec = analyzer.analyze(*mod_prog, imports);
            if (mod_errors.has_errors()) {
                print_errors(mod_errors);
                return 1;
            }

            // Save artifact
            cactus::ErrorReporter art_errors;
            cactus::ModuleArtifact artifact(art_errors);
            if (!artifact.save(dec, mod.qualified_name, build_dir)) {
                print_errors(art_errors);
                return 1;
            }
            artifact_paths.push_back(build_dir / (mod.qualified_name + ".cmod"));

            // Preserve full declaration ASTs for final codegen so imported
            // units/systems/extern systems from stdlib modules are also emitted.
            merged_codegen_prog->declarations.insert(merged_codegen_prog->declarations.end(),
                                                     std::make_move_iterator(mod_prog->declarations.begin()),
                                                     std::make_move_iterator(mod_prog->declarations.end()));

            compiled[mod.qualified_name] = std::move(dec);
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
        // declarations (units, systems, extern systems, consts, etc.) are
        // still emitted after multi-module linking. Artifacts intentionally do
        // not serialize AST structure.
        decorated.ast = merged_codegen_prog.get();

    } else {
        // ── 6.3: Single-file backward-compatible pipeline ─────────────────────
        cactus::SemanticAnalyzer analyzer(errors);
        decorated = analyzer.analyze(*root_prog);
        if (errors.has_errors()) {
            print_errors(errors);
            return 1;
        }
    }

    // ── Code generation ───────────────────────────────────────────────────────
    std::string generated;
    if (backend == "cpp-manual") {
        generated = cactus::CppManualCodegen::generate(decorated);
    } else {
        generated = cactus::CppEnttCodegen::generate(decorated);
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
