#pragma once

// Parses the stdlib sources for tests that check tables against them.
// Needs CACTUS_TEST_SOURCE_DIR.

#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/native_field_reads.hpp"
#include "frontend/parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
// -- Catch2 assertion macros intentionally expand through do-while.
namespace cactus_test {

struct ParsedModule {
    std::string name;
    cactus::ProgramNode program;
};

inline ParsedModule parse_module(const std::string& source, const std::string& filename) {
    cactus::ErrorReporter errors;
    cactus::Lexer lexer(source, filename, errors);
    cactus::Parser parser(lexer.tokenize(), errors);
    ParsedModule parsed{.program = parser.parse_program()};
    REQUIRE_FALSE(errors.has_errors());
    for (const auto& declaration : parsed.program.declarations) {
        if (const auto* module = std::get_if<cactus::ModuleNode>(&declaration)) {
            parsed.name = module->name;
        }
    }
    return parsed;
}

inline std::vector<ParsedModule> parse_stdlib() {
    std::vector<ParsedModule> modules;
    const auto root = std::filesystem::path(CACTUS_TEST_SOURCE_DIR) / "stdlib";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".cactus" || entry.path().string().contains("build")) {
            continue;
        }
        std::ifstream file(entry.path());
        std::stringstream source;
        source << file.rdbuf();
        modules.push_back(parse_module(source.str(), entry.path().string()));
    }
    return modules;
}

// The canonical ids of every stdlib extern function that observes `world`.
inline std::vector<std::string> world_natives(const std::vector<ParsedModule>& modules) {
    std::vector<std::string> natives;
    for (const auto& module : modules) {
        if (!cactus::stdlib_module_observes_world(module.name)) {
            continue;
        }
        for (const auto& declaration : module.program.declarations) {
            const auto* func = std::get_if<cactus::FuncNode>(&declaration);
            if (func != nullptr && func->is_extern) {
                natives.push_back(module.name + "." + func->name);
            }
        }
    }
    return natives;
}

}  // namespace cactus_test
// NOLINTEND(cppcoreguidelines-avoid-do-while)
