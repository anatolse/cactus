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
    bool has_default = false;  // true if field has a default value in trait definition
};

struct ResolvedParam {
    std::string name;
    TypeInfo type;
};

struct ResolvedFunc {
    std::string name;
    bool is_pub = false;
    bool is_extern = false;
    std::vector<ResolvedParam> params;
    std::optional<TypeInfo> return_type;
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
    std::vector<std::string> after_systems;  // explicit ordering: this system runs after these
};

struct DecoratedProgram {
    std::unordered_map<std::string, ResolvedTrait> traits;
    std::unordered_map<std::string, ResolvedStruct> structs;
    std::unordered_map<std::string, ResolvedEnum> enums;
    std::unordered_map<std::string, ResolvedFunc> funcs;
    std::unordered_set<std::string> pub_events;  // pub event names (for ImportedSymbols export)
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
    std::unordered_map<std::string, ResolvedFunc>   funcs;    // pub extern funcs
    std::unordered_set<std::string>                 events;   // pub event names
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
    std::unordered_map<std::string, std::vector<std::string>> func_providers;

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
    void validateOrderByClause(const SystemNode& system);
    void validateOrderByClause(const ExternSystemNode& system);
    void validate_event_usage(ProgramNode& program);
    void validate_event_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                              const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                              const std::unordered_map<std::string, TypeInfo>& local_bindings,
                              const ResolvedStruct* handler_event,
                              const std::string& system_name);
    void validate_trait_match_stmt(const TraitMatchStmt& stmt,
                                   const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                   const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                   const ResolvedStruct* handler_event,
                                   const std::string& system_name,
                                   bool in_system_handler);

    // Phase 3: Dynamic ECS validations (dynamic-ecs-language change)
    void validate_template_unit_declarations(ProgramNode& program);
    void validate_spawn_sites(ProgramNode& program);
    void validate_stmt_contexts(ProgramNode& program);
    void validate_trait_modifier_rules(ProgramNode& program);

    // task 11.12: field access not allowed in systems with no filter clause
    void check_no_field_access(
        const std::vector<std::unique_ptr<StmtNode>>& stmts,
        const std::string& sys_name);

    // Dynamic ECS helpers
    bool is_trait_declared(const std::string& name) const;
    std::unordered_set<std::string> get_archetype_fields(
        const std::vector<ArchetypeTraitEntry>& traits) const;
    const ResolvedTrait* find_resolved_trait(const std::string& name) const;
    const ResolvedStruct* find_resolved_event(const std::string& name) const;
    TypeInfo infer_expr_type(const ExprNode& expr,
                             const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                             const std::unordered_map<std::string, TypeInfo>& local_bindings,
                             const ResolvedStruct* handler_event) const;
    void validate_spawn_stmts(
        const std::vector<std::unique_ptr<StmtNode>>& stmts,
        const std::string& context_name);
    void validate_spawn_exprs(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                              const std::string& context_name);
    void validate_spawn_expr(const SpawnExpr& spawn, const SourceLocation& location);
    void validate_context_stmts(
        const std::vector<std::unique_ptr<StmtNode>>& stmts,
        const std::string& context_name,
        bool in_system_handler);
    void validate_trait_default_values(ProgramNode& program);

    // Phase 4: Build dependency graph
    void build_dependency_graph(ProgramNode& program);
    void collect_system_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                             SystemDependency& dep);

    // Phase 5: after: validation
    void validate_after_clauses(ProgramNode& program);

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
    std::unordered_set<std::string> system_names_;

    // Asset and input declaration tracking (dsl-spec-new-features)
    // Maps identifier name → resolved TypeKind (e.g., "PlayerMesh" → MeshId)
    std::unordered_map<std::string, TypeKind> asset_decl_types_;
    // Maps identifier name → InputButton or InputAxis
    std::unordered_map<std::string, TypeKind> input_decl_types_;

    // For recursion detection
    std::unordered_map<std::string, std::unordered_set<std::string>> call_graph_;

    // ── Dynamic ECS tracking (dynamic-ecs-language change) ──────────────────
    // Separate sets for templates vs units (spawn only works on templates)
    std::unordered_set<std::string> template_names_;
    std::unordered_set<std::string> unit_names_;

    // Module names/aliases declared via `use` (for `load` reachability check)
    std::unordered_set<std::string> use_names_;

    // Archetype trait entries: archetype_name → nested trait entries list
    std::unordered_map<std::string, const std::vector<ArchetypeTraitEntry>*> archetype_traits_;

    // Template required fields (var with no default and not in config):
    // template_name → set of field names that must be provided at spawn site
    std::unordered_map<std::string, std::unordered_set<std::string>> template_required_fields_;

    // Event declarations as struct-like field maps for emit payload validation.
    std::unordered_map<std::string, ResolvedStruct> event_structs_;
};

}  // namespace cactus
