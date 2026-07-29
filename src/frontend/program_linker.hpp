#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

/// Merges multiple compiled `.cmod` module artifacts into a single
/// `DecoratedProgram` suitable for code generation.
///
/// The linker:
///   - Loads artifacts one at a time (incremental)
///   - Merges traits, structs, enums, dependency graphs, and string pools
///   - Detects duplicate pub symbol names across modules
///   - Preserves declaration ordering (dependency modules first)
///
/// Note: The `ProgramNode* ast` field of the merged result is left null
/// because artifact deserialization does not preserve the original AST.
/// Backends that only use `DecoratedProgram` (the common case) are unaffected.
class ProgramLinker {
public:
    explicit ProgramLinker(ErrorReporter& errors);

    /// Load and merge the given .cmod artifact files in order.
    /// Returns the merged DecoratedProgram on success, or nullopt on error.
    /// artifact_paths should be in dependency order (leaves first).
    std::optional<DecoratedProgram> link(const std::vector<std::filesystem::path>& artifact_paths);

    /// Merge src into target.
    /// src_module_name is used in error messages for duplicate symbols.
    /// Returns false if a duplicate symbol was detected.
    bool merge_into(DecoratedProgram& target, const DecoratedProgram& src, const std::string& src_module_name);

private:
    bool rebuild_execution_graph(DecoratedProgram& program, bool validate_references);

    std::vector<ScheduleEdge> linked_explicit_edges_;

    ErrorReporter& errors_;

    /// Symbol origin tracking: symbol name → module that first defined it.
    std::unordered_map<std::string, std::string> symbol_origins_;

    /// Module names already merged. Re-linking the same artifact/module is
    /// idempotent so implicit std.core plus explicit `use std.core` has one
    /// authoritative symbol source.
    std::unordered_set<std::string> merged_modules_;
};

}  // namespace cactus
