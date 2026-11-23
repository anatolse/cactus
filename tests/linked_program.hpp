#pragma once

// Compiles and links a program the way the CLI does: std.core first, every module in
// dependency order, artifacts linked with the merged AST. Needs CACTUS_TEST_SOURCE_DIR.

#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/module_resolver.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace cactus_test {

struct LinkedProgram {
    std::unique_ptr<cactus::ProgramNode> ast = std::make_unique<cactus::ProgramNode>();
    std::optional<cactus::DecoratedProgram> program;
    std::vector<cactus::Diagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return std::ranges::any_of(diagnostics,
                                   [](const auto& d) { return d.level == cactus::DiagnosticLevel::Error; });
    }
    [[nodiscard]] std::string describe() const {
        std::string text;
        for (const auto& d : diagnostics) {
            text += d.location.filename + ":" + std::to_string(d.location.line) + ": " + d.message + "\n";
        }
        return text;
    }
};

inline std::filesystem::path stdlib_root() {
    return std::filesystem::path(CACTUS_TEST_SOURCE_DIR) / "stdlib";
}

// Writes `<dir>/all_stdlib.cactus`, a module that uses every standard library module, and returns its path.
inline std::filesystem::path write_all_stdlib_program(const std::filesystem::path& dir) {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto root = dir / "all_stdlib.cactus";
    std::ofstream out(root);
    out << "module all_stdlib\n";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(stdlib_root())) {
        if (entry.path().extension() == ".cactus" && !entry.path().string().contains("build")) {
            auto name = std::filesystem::relative(entry.path(), stdlib_root()).replace_extension().generic_string();
            std::ranges::replace(name, '/', '.');
            out << "use " << name << "\n";
        }
    }
    return root;
}

// `file:line: message` of every diagnostic `keep` selects.
template <typename Keep>
std::vector<std::string> located_messages(const std::vector<cactus::Diagnostic>& diagnostics, Keep keep) {
    std::vector<std::string> found;
    for (const auto& diagnostic : diagnostics) {
        if (keep(diagnostic)) {
            found.push_back(diagnostic.location.filename + ":" + std::to_string(diagnostic.location.line) + ": " +
                            diagnostic.message);
        }
    }
    return found;
}

class ProgramPipeline {
public:
    explicit ProgramPipeline(std::filesystem::path build_dir)
        : build_dir_(std::move(build_dir)) {
        std::filesystem::create_directories(build_dir_);
    }

    // Links `root` and the modules it uses, searching `search` after the stdlib.
    LinkedProgram link(const std::filesystem::path& root, const std::vector<std::filesystem::path>& search = {}) {
        if (!compile_all(root, search)) {
            return std::move(linked_);
        }
        cactus::ErrorReporter link_errors;
        cactus::ProgramLinker linker(link_errors);
        linked_.program = linker.link(artifacts_, linked_.ast.get());
        append(link_errors);
        return std::move(linked_);
    }

    // Merges the declarations of every module like `link`, without the whole-program passes.
    LinkedProgram merge(const std::filesystem::path& root, const std::vector<std::filesystem::path>& search = {}) {
        if (!compile_all(root, search)) {
            return std::move(linked_);
        }
        cactus::ErrorReporter merge_errors;
        cactus::ProgramLinker linker(merge_errors);
        cactus::DecoratedProgram merged;
        for (const auto& name : order_) {
            linker.merge_into(merged, compiled_.at(name), name);
        }
        merged.ast      = linked_.ast.get();
        linked_.program = std::move(merged);
        append(merge_errors);
        return std::move(linked_);
    }

private:
    bool compile_all(const std::filesystem::path& root, const std::vector<std::filesystem::path>& search) {
        std::vector<std::filesystem::path> paths{stdlib_root()};
        paths.insert(paths.end(), search.begin(), search.end());
        cactus::ErrorReporter resolve_errors;
        cactus::ModuleResolver resolver(resolve_errors);
        const auto modules = resolver.resolve(root.string(), paths);
        if (resolve_errors.has_errors()) {
            linked_.diagnostics = resolve_errors.diagnostics();
            return false;
        }
        if (!compile("std.core", stdlib_root() / "std" / "core.cactus", {})) {
            return false;
        }
        for (const auto& module : modules) {
            if (!compiled_.contains(module.qualified_name) &&
                !compile(module.qualified_name, module.file_path, module.dependencies)) {
                return false;
            }
        }
        return true;
    }

    void append(const cactus::ErrorReporter& errors) {
        const auto& more = errors.diagnostics();
        linked_.diagnostics.insert(linked_.diagnostics.end(), more.begin(), more.end());
    }

    void add_import(cactus::ModuleImports& imports, const std::string& qualifier, const std::string& module) const {
        const auto& program = compiled_.at(module);
        imports.add(
            qualifier, cactus::ModuleArtifact::extract_pub_symbols(module, program), {}, program.non_pub_templates);
    }

    bool compile(const std::string& name,
                 const std::filesystem::path& path,
                 const std::vector<std::string>& dependencies) {
        std::ifstream file(path);
        std::stringstream source;
        source << file.rdbuf();
        cactus::ErrorReporter errors;
        cactus::Lexer lexer(source.str(), path.string(), errors);
        cactus::Parser parser(lexer.tokenize(), errors);
        auto ast = std::make_unique<cactus::ProgramNode>(parser.parse_program());
        if (errors.has_errors()) {
            append(errors);
            return false;
        }
        cactus::ModuleImports imports;
        if (name != "std.core") {
            add_import(imports, "std.core", "std.core");
        }
        for (const auto& dependency : dependencies) {
            if (!compiled_.contains(dependency)) {
                continue;
            }
            bool aliased = false;
            for (const auto& declaration : ast->declarations) {
                const auto* use = std::get_if<cactus::UseNode>(&declaration);
                if (use != nullptr && use->module_name == dependency) {
                    add_import(imports, use->alias.value_or(dependency), dependency);
                    aliased = true;
                }
            }
            if (!aliased) {
                add_import(imports, dependency, dependency);
            }
        }
        cactus::SemanticAnalyzer analyzer(errors);
        auto program = analyzer.analyze(*ast, imports);
        append(errors);
        if (errors.has_errors()) {
            return false;
        }
        cactus::ModuleArtifact artifact(errors);
        if (!artifact.save(program, name, build_dir_)) {
            append(errors);
            return false;
        }
        artifacts_.push_back(build_dir_ / (name + ".cmod"));
        auto& merged = linked_.ast->declarations;
        merged.insert(merged.end(),
                      std::make_move_iterator(ast->declarations.begin()),
                      std::make_move_iterator(ast->declarations.end()));
        compiled_.emplace(name, std::move(program));
        order_.push_back(name);
        return true;
    }

    std::filesystem::path build_dir_;
    std::map<std::string, cactus::DecoratedProgram> compiled_;
    std::vector<std::filesystem::path> artifacts_;
    std::vector<std::string> order_;
    LinkedProgram linked_;
};

// Writes each `name -> source` pair to `<dir>/<name>.cactus` and links `root` among them.
inline LinkedProgram link_sources(const std::filesystem::path& dir,
                                  const std::map<std::string, std::string>& modules,
                                  const std::string& root) {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    for (const auto& [name, source] : modules) {
        std::ofstream(dir / (name + ".cactus")) << source;
    }
    ProgramPipeline pipeline(dir / "build");
    return pipeline.link(dir / (root + ".cactus"), {dir});
}

}  // namespace cactus_test
