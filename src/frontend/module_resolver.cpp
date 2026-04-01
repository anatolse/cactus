#include "frontend/module_resolver.h"

#include "frontend/lexer.h"
#include "frontend/parser.h"

#include <algorithm>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace cactus {

namespace fs = std::filesystem;

ModuleResolver::ModuleResolver(ErrorReporter& errors) : errors_(errors) {}

// ── Dependency Discovery ────────────────────────────────────────────────────

std::vector<std::string> ModuleResolver::extract_dependencies(const ProgramNode& program) {
    std::vector<std::string> deps;
    for (const auto& decl : program.declarations) {
        if (const auto* use = std::get_if<UseNode>(&decl)) {
            deps.push_back(use->module_name);  // alias is ignored for resolution
        }
    }
    return deps;
}

// ── File Location ───────────────────────────────────────────────────────────

fs::path ModuleResolver::module_name_to_path(const std::string& module_name) {
    std::string result = module_name;
    std::ranges::replace(result, '.', static_cast<char>(fs::path::preferred_separator));
    return {result + ".cactus"};
}

fs::path ModuleResolver::locate_file(const std::string& module_name, const std::vector<fs::path>& search_dirs) {
    auto relative = module_name_to_path(module_name);
    for (const auto& dir : search_dirs) {
        auto candidate = dir / relative;
        if (fs::exists(candidate)) {
            return fs::canonical(candidate);
        }
    }
    return {};
}

// ── Module Name Inference ───────────────────────────────────────────────────

std::string ModuleResolver::infer_module_name(const fs::path& search_root,
                                               const fs::path& file_path) {
    auto rel = fs::relative(file_path, search_root);
    auto str = rel.generic_string();  // use forward slashes

    // Remove .cactus extension
    const std::string EXT = ".cactus";
    if (str.size() > EXT.size() && str.substr(str.size() - EXT.size()) == EXT) {
        str = str.substr(0, str.size() - EXT.size());
    }

    // Replace / with .
    std::ranges::replace(str, '/', '.');
    return str;
}

// ── Module Name Validation ──────────────────────────────────────────────────

bool ModuleResolver::validate_module_name(const ProgramNode& program,
                                           const std::string& inferred_name,
                                           const fs::path& file_path) {
    for (const auto& decl : program.declarations) {
        if (const auto* mod = std::get_if<ModuleNode>(&decl)) {
            if (mod->name != inferred_name) {
                errors_.error(mod->location,
                              "module name '" + mod->name + "' does not match filename '" +
                                  inferred_name + "' (from " + file_path.filename().string() + ")");
                return false;
            }
            return true;
        }
    }
    // No module declaration — infer from filename (this is valid)
    return true;
}

// ── Recursive Resolution ────────────────────────────────────────────────────

bool ModuleResolver::resolve_module(const std::string& module_name,
                                     const std::vector<fs::path>& search_dirs,
                                     std::vector<std::string>& visiting_stack) {
    // Already fully resolved?
    auto vit = visited_.find(module_name);
    if (vit != visited_.end()) {
        if (vit->second) {
            return true;  // fully resolved
        }

        // Currently being visited → circular dependency
        std::string cycle;
        bool in_cycle = false;
        for (auto& name : visiting_stack) {
            if (name == module_name) {
                in_cycle = true;
            }
            if (in_cycle) {
                cycle += name + " → ";
            }
        }
        cycle += module_name;
        errors_.error({}, "circular dependency: " + cycle);
        return false;
    }

    // Mark as being visited
    visited_[module_name] = false;
    visiting_stack.push_back(module_name);

    // Locate the file
    auto file_path = locate_file(module_name, search_dirs);
    if (file_path.empty()) {
        errors_.error({}, "module '" + module_name + "' not found");
        visiting_stack.pop_back();
        return false;
    }

    // Read and parse
    std::ifstream ifs(file_path);
    if (!ifs) {
        errors_.error({}, "cannot read file: " + file_path.string());
        visiting_stack.pop_back();
        return false;
    }
    std::string source((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    ErrorReporter parse_errors;
    Lexer lexer(source, file_path.string(), parse_errors);
    auto tokens = lexer.tokenize();
    if (parse_errors.has_errors()) {
        // Propagate errors
        errors_.error({}, "errors while lexing " + file_path.string());
        visiting_stack.pop_back();
        return false;
    }

    Parser parser(std::move(tokens), parse_errors);
    auto program = parser.parse_program();
    if (parse_errors.has_errors()) {
        errors_.error({}, "errors while parsing " + file_path.string());
        visiting_stack.pop_back();
        return false;
    }

    // Determine the search root for this file (the directory containing it relative to its module path)
    // For validation, find which search dir contains this file
    fs::path file_search_root;
    auto relative_path = module_name_to_path(module_name);
    for (const auto& dir : search_dirs) {
        std::error_code ec;
        auto candidate = fs::canonical(dir / relative_path, ec);
        if (!ec && candidate == file_path) {
            file_search_root = dir;
            break;
        }
    }

    // Validate module name declaration
    if (!file_search_root.empty()) {
        auto inferred = infer_module_name(file_search_root, file_path);
        if (!validate_module_name(program, inferred, file_path)) {
            visiting_stack.pop_back();
            return false;
        }
    }

    // Extract dependencies
    auto deps = extract_dependencies(program);

    // Recursively resolve dependencies
    for (auto& dep : deps) {
        if (!resolve_module(dep, search_dirs, visiting_stack)) {
            visiting_stack.pop_back();
            return false;
        }
    }

    // Store module info
    ModuleInfo info;
    info.qualified_name = module_name;
    info.file_path = file_path;
    info.dependencies = std::move(deps);
    modules_[module_name] = std::move(info);

    // Mark as fully resolved
    visited_[module_name] = true;
    visiting_stack.pop_back();
    return true;
}

// ── Topological Sort (Kahn's algorithm) ─────────────────────────────────────

std::vector<std::string> ModuleResolver::topological_sort() const {
    // Build in-degree map
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> dependents;

    for (const auto& [name, info] : modules_) {
        if (!in_degree.contains(name)) {
            in_degree[name] = 0;
        }
        for (const auto& dep : info.dependencies) {
            dependents[dep].push_back(name);
            in_degree[name]++;
        }
    }

    // Start with nodes that have no dependencies
    std::queue<std::string> ready;
    // Use sorted order for determinism
    std::vector<std::string> all_names;
    all_names.reserve(modules_.size());
    for (const auto& [name, _] : modules_) {
        all_names.push_back(name);
    }
    std::ranges::sort(all_names);

    for (auto& name : all_names) {
        if (in_degree[name] == 0) {
            ready.push(name);
        }
    }

    std::vector<std::string> order;
    order.reserve(modules_.size());

    while (!ready.empty()) {
        auto current = ready.front();
        ready.pop();
        order.push_back(current);

        if (dependents.contains(current)) {
            // Sort dependents for determinism
            auto& deps = dependents[current];
            std::vector<std::string> sorted_deps(deps.begin(), deps.end());
            std::ranges::sort(sorted_deps);
            for (auto& dep : sorted_deps) {
                in_degree[dep]--;
                if (in_degree[dep] == 0) {
                    ready.push(dep);
                }
            }
        }
    }

    return order;
}

// ── Main Resolution Entry Point ─────────────────────────────────────────────

std::vector<ModuleInfo> ModuleResolver::resolve(const fs::path& root_file,
                                                 const std::vector<fs::path>& search_paths) {
    modules_.clear();
    visited_.clear();

    if (!fs::exists(root_file)) {
        errors_.error({}, "root file not found: " + root_file.string());
        return {};
    }

    auto canonical_root = fs::canonical(root_file);
    auto root_dir = canonical_root.parent_path();

    // Build search dirs: root dir first, then additional paths
    std::vector<fs::path> search_dirs;
    search_dirs.push_back(root_dir);
    for (const auto& sp : search_paths) {
        if (fs::exists(sp)) {
            search_dirs.push_back(fs::canonical(sp));
        }
    }

    // Infer root module name from filename
    auto root_name = canonical_root.stem().string();  // "main" from "main.cactus"

    // Read and parse root file
    std::ifstream ifs(canonical_root);
    if (!ifs) {
        errors_.error({}, "cannot read file: " + canonical_root.string());
        return {};
    }
    std::string source((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    ErrorReporter parse_errors;
    Lexer lexer(source, canonical_root.string(), parse_errors);
    auto tokens = lexer.tokenize();
    if (parse_errors.has_errors()) {
        errors_.error({}, "errors while lexing " + canonical_root.string());
        return {};
    }

    Parser parser(std::move(tokens), parse_errors);
    auto program = parser.parse_program();
    if (parse_errors.has_errors()) {
        errors_.error({}, "errors while parsing " + canonical_root.string());
        return {};
    }

    // Check for explicit module declaration to get root name
    for (auto& decl : program.declarations) {
        if (auto* mod = std::get_if<ModuleNode>(&decl)) {
            root_name = mod->name;
            break;
        }
    }

    // Extract root dependencies and store root module
    auto deps = extract_dependencies(program);
    ModuleInfo root_info;
    root_info.qualified_name = root_name;
    root_info.file_path = canonical_root;
    root_info.dependencies = deps;

    visited_[root_name] = false;

    // Resolve all dependencies recursively
    std::vector<std::string> visiting_stack = {root_name};
    for (auto& dep : deps) {
        if (!resolve_module(dep, search_dirs, visiting_stack)) {
            return {};
        }
    }

    // Store root and mark resolved
    modules_[root_name] = std::move(root_info);
    visited_[root_name] = true;

    // Topological sort
    auto order = topological_sort();

    // Convert to ModuleInfo list
    std::vector<ModuleInfo> result;
    result.reserve(order.size());
    for (auto& name : order) {
        result.push_back(modules_.at(name));
    }
    return result;
}

}  // namespace cactus
