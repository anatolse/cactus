#pragma once

#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace cactus {

/// Information about a resolved module.
struct ModuleInfo {
    std::string qualified_name;             // e.g. "enemies.walker"
    std::filesystem::path file_path;        // canonical path to .cactus file
    std::vector<std::string> dependencies;  // qualified names of modules this one depends on
};

/// Resolves a multi-module project into a compilation-ordered list of modules.
///
/// Given a root .cactus file and optional search paths, the resolver:
/// 1. Discovers dependencies from `use` declarations
/// 2. Locates .cactus files (dots → directory separators)
/// 3. Builds a dependency DAG (deduplicating diamond deps)
/// 4. Detects circular dependencies
/// 5. Validates module name declarations
/// 6. Returns modules in topological order (leaves first)
class ModuleResolver {
public:
    explicit ModuleResolver(ErrorReporter& errors);

    /// Resolve all modules starting from root_file.
    /// search_paths: directories to search after the root's directory (from --module-path).
    /// Returns modules in compilation order (dependencies first), or empty on error.
    std::vector<ModuleInfo> resolve(const std::filesystem::path& root_file,
                                    const std::vector<std::filesystem::path>& search_paths = {});

    // ── Utility methods (public for testing) ────────────────────────────

    /// Extract dependency module names from a parsed program's UseNode declarations.
    static std::vector<std::string> extract_dependencies(const ProgramNode& program);

    /// Convert a dotted module name to a relative filesystem path.
    /// e.g. "enemies.walker" → "enemies/walker.cactus"
    static std::filesystem::path module_name_to_path(const std::string& module_name);

    /// Locate a module file in search directories.
    /// Returns empty path if not found.
    static std::filesystem::path locate_file(const std::string& module_name,
                                             const std::vector<std::filesystem::path>& search_dirs);

    /// Infer qualified module name from file path relative to a search root.
    /// e.g. ("/game", "/game/enemies/walker.cactus") → "enemies.walker"
    static std::string infer_module_name(const std::filesystem::path& search_root,
                                         const std::filesystem::path& file_path);

    /// Validate that a module declaration (if present) matches the inferred name.
    bool validate_module_name(const ProgramNode& program,
                              const std::string& inferred_name,
                              const std::filesystem::path& file_path);

private:
    /// Recursively resolve a module and its dependencies.
    /// Returns false on error (circular dep, file not found, etc.)
    bool resolve_module(const std::string& module_name,
                        const std::vector<std::filesystem::path>& search_dirs,
                        std::vector<std::string>& visiting_stack);

    /// Topological sort using Kahn's algorithm. Returns empty on cycle.
    std::vector<std::string> topological_sort() const;

    ErrorReporter& errors_;

    // Module cache: qualified_name → ModuleInfo
    std::unordered_map<std::string, ModuleInfo> modules_;

    // Track which modules are being visited (for cycle detection)
    std::unordered_map<std::string, bool> visited_;  // true = fully resolved
};

}  // namespace cactus
