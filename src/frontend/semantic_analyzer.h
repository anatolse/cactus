#pragma once

#include "common/error_reporter.h"
#include "common/string_pool.h"
#include "common/types.h"
#include "frontend/ast.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

// ── Decorated AST (resolved output of the semantic analyzer) ───────────────

struct ResolvedField {
    std::string name;
    TypeInfo type;
    bool is_let = false;
    bool is_var = false;
    bool is_persist = false;
    bool is_sync = false;
    bool is_pub = false;
};

struct ResolvedTrait {
    std::string name;
    std::vector<ResolvedField> fields;
    bool is_pub = false;
};

struct ResolvedStruct {
    std::string name;
    std::vector<ResolvedField> fields;
};

struct ResolvedEnum {
    std::string name;
    std::vector<std::string> variants;
};

struct SystemDependency {
    std::string system_name;
    std::unordered_set<std::string> reads;
    std::unordered_set<std::string> writes;
    std::unordered_set<std::string> emits;
};

struct DecoratedProgram {
    std::unordered_map<std::string, ResolvedTrait> traits;
    std::unordered_map<std::string, ResolvedStruct> structs;
    std::unordered_map<std::string, ResolvedEnum> enums;
    std::vector<SystemDependency> dependency_graph;
    StringPool string_pool;
    ProgramNode* ast = nullptr;  // non-owning pointer to original AST
};

// ── Imported Symbols (pub exports from a single module) ────────────────────

/// Public symbols extracted from a compiled module artifact.
/// Only `pub`-marked declarations are included.
struct ImportedSymbols {
    std::string module_name;  // qualified name of the source module

    std::unordered_map<std::string, ResolvedTrait>  traits;   // pub traits
    std::unordered_map<std::string, ResolvedStruct> structs;  // pub structs
    std::unordered_map<std::string, ResolvedEnum>   enums;    // pub enums
};

// ── Module Imports (aggregate for one compilation unit) ────────────────────

/// Aggregate of imported modules' pub symbols for a single compilation unit.
/// Keyed by qualifier: the declared module name or a `use ... as` alias.
///
/// Pass ModuleImports{} (the default) for single-file backward-compatible mode.
struct ModuleImports {
    /// qualifier → pub symbols for that module
    std::unordered_map<std::string, ImportedSymbols> modules;

    /// qualifier → non-pub trait names (for "did you mean to add pub?" errors)
    std::unordered_map<std::string, std::unordered_set<std::string>> non_pub_trait_names;

    /// Global uniqueness index: symbol name → list of qualifiers that export it
    std::unordered_map<std::string, std::vector<std::string>> trait_providers;
    std::unordered_map<std::string, std::vector<std::string>> struct_providers;
    std::unordered_map<std::string, std::vector<std::string>> enum_providers;

    [[nodiscard]] bool empty() const { return modules.empty(); }

    /// Add one module's pub symbols under the given qualifier (module name or alias).
    /// non_pub: non-pub trait names in this module, for helpful error diagnostics.
    void add(const std::string& qualifier, ImportedSymbols pub_syms,
             std::unordered_set<std::string> non_pub = {});
};

// ── Semantic Analyzer ───────────────────────────────────────────────────────

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(ErrorReporter& errors);

    /// Analyze a program with optional multi-module imported symbols.
    /// Omit imports (or pass ModuleImports{}) for single-file backward-compatible mode.
    DecoratedProgram analyze(ProgramNode& program,
                             const ModuleImports& imports = ModuleImports{});

private:
    // Phase 1: Collect type declarations
    void collect_types(ProgramNode& program);

    // Phase 2: Resolve types in fields
    void resolve_all_types(ProgramNode& program);
    TypeInfo resolve_type_ref(const TypeRef& ref);

    // Import-aware type resolution helpers
    TypeInfo resolve_qualified_type(const std::string& qualifier,
                                    const std::string& sym_name,
                                    const SourceLocation& loc);
    TypeInfo resolve_imported_type(const std::string& name, const SourceLocation& loc);

    // Phase 3: Semantic checks
    void check_const_strings(ProgramNode& program);
    void check_const_strings_expr(const ExprNode& expr, bool in_const);
    void check_func_purity(ProgramNode& program);
    void check_func_purity_stmt(const StmtNode& stmt, const std::string& func_name);
    void check_func_purity_expr(const ExprNode& expr, const std::string& func_name);
    void check_no_recursion(ProgramNode& program);
    void check_persist_sync(ProgramNode& program);
    void validate_system_filters(ProgramNode& program);
    void validate_event_usage(ProgramNode& program);

    // Phase 4: Build dependency graph
    void build_dependency_graph(ProgramNode& program);
    void collect_system_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                             SystemDependency& dep);

    // Helpers
    bool is_known_type(const std::string& name) const;

    /// Validate one filter entry against local traits and imports.
    /// Sets out_simple_name to the unqualified trait name on success.
    bool resolve_filter_entry(const FilterEntry& entry, std::string& out_simple_name);

    ErrorReporter& errors_;
    DecoratedProgram result_;
    ModuleImports imports_;

    // Known type names (populated during Phase 1)
    std::unordered_set<std::string> struct_names_;
    std::unordered_set<std::string> enum_names_;
    std::unordered_set<std::string> trait_names_;
    std::unordered_set<std::string> event_names_;
    std::unordered_set<std::string> func_names_;

    // For recursion detection
    std::unordered_map<std::string, std::unordered_set<std::string>> call_graph_;
};

}  // namespace cactus
