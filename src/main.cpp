#include "backends/cpp-entt/cpp_entt_codegen.h"
#include "backends/cpp-manual/cpp_manual_codegen.h"
#include "common/error_reporter.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "frontend/semantic_analyzer.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <input.cactus> [options]\n"
              << "\nOptions:\n"
              << "  --backend <cpp-manual|cpp-entt>  Code generation backend (default: cpp-manual)\n"
              << "  --output <file>                  Output file (default: stdout)\n"
              << "  --help                           Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_file;
    std::string backend = "cpp-manual";
    std::string output_file;

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

    // Read input file
    std::ifstream ifs(input_file);
    if (!ifs.is_open()) {
        std::cerr << "error: cannot open file '" << input_file << "'\n";
        return 1;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string source = ss.str();

    // Lex
    cactus::ErrorReporter errors;
    cactus::Lexer lexer(source, input_file, errors);
    auto tokens = lexer.tokenize();
    if (errors.has_errors()) {
        for (auto& d : errors.diagnostics()) {
            std::cerr << d.location.filename << ":" << d.location.line << ":" << d.location.column << ": "
                      << (d.level == cactus::DiagnosticLevel::Error ? "error" : "warning") << ": " << d.message
                      << "\n";
        }
        return 1;
    }

    // Parse
    cactus::Parser parser(std::move(tokens), errors);
    auto program = parser.parse_program();
    if (errors.has_errors()) {
        for (auto& d : errors.diagnostics()) {
            std::cerr << d.location.filename << ":" << d.location.line << ":" << d.location.column << ": "
                      << (d.level == cactus::DiagnosticLevel::Error ? "error" : "warning") << ": " << d.message
                      << "\n";
        }
        return 1;
    }

    // Semantic analysis
    cactus::SemanticAnalyzer analyzer(errors);
    auto decorated = analyzer.analyze(program);
    if (errors.has_errors()) {
        for (auto& d : errors.diagnostics()) {
            std::cerr << d.location.filename << ":" << d.location.line << ":" << d.location.column << ": "
                      << (d.level == cactus::DiagnosticLevel::Error ? "error" : "warning") << ": " << d.message
                      << "\n";
        }
        return 1;
    }

    // Code generation
    std::string generated;
    if (backend == "cpp-manual") {
        generated = cactus::CppManualCodegen::generate(decorated);
    } else {
        generated = cactus::CppEnttCodegen::generate(decorated);
    }

    // Output
    if (output_file.empty()) {
        std::cout << generated;
    } else {
        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            std::cerr << "error: cannot open output file '" << output_file << "'\n";
            return 1;
        }
        ofs << generated;
        std::cerr << "Generated " << output_file << " (" << backend << " backend)\n";
    }

    return 0;
}
