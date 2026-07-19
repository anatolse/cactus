#pragma once

#include "common/error_reporter.hpp"
#include "common/string_pool.hpp"
#include "common/types.hpp"
#include "frontend/ast.hpp"
#include "frontend/symbol_identity.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

// ── Decorated AST (resolved output of the semantic analyzer) ───────────────

struct ResolvedField {
    std::string name;
    TypeInfo type;
    bool is_let      = false;
    bool is_var      = false;
    bool is_persist  = false;
    bool is_sync     = false;
    bool is_pub      = false;
    bool has_default = false;
};

// Canonical identity mixin — present on all module-scope resolved declarations.
struct CanonicalIdentity {
    std::string module_name;   // declaring module path
    std::string canonical_id;  // "<module_name>.<local_name>"
    std::optional<SymbolId> symbol_id;
};

struct ResolvedParam {
    std::string name;
    TypeInfo type;
};

struct ResolvedFunc : CanonicalIdentity {
    std::string name;
    bool is_pub    = false;
    bool is_extern = false;
    bool is_stdlib = false;
    std::vector<ResolvedParam> params;
    std::optional<TypeInfo> return_type;
};

struct ResolvedTrait : CanonicalIdentity {
    std::string name;
    std::vector<ResolvedField> fields;
    bool is_pub    = false;
    bool is_stdlib = false;
};

struct ResolvedStruct : CanonicalIdentity {
    std::string name;
    std::vector<ResolvedField> fields;
};

struct ResolvedEnum : CanonicalIdentity {
    std::string name;
    std::vector<std::string> variants;
};

/// Codegen-facing source module view. The AST remains the parsed syntax tree,
/// but semantic analysis mutates its module-scope reference sites with resolved
/// SymbolIds. Linkers preserve these per-module views instead of fabricating a
/// combined raw AST.
struct ResolvedSourceModule {
    std::string module_name;
    ProgramNode* ast = nullptr;  // non-owning; owned by the CLI/test pipeline
};

struct SystemDependency {
    std::string system_name;
    std::optional<SymbolId> system_id;  // resolved system identity
    std::unordered_set<std::string> reads;
    std::unordered_set<std::string> writes;
    std::unordered_set<std::string> emits;
    std::vector<std::string> after_systems;           // explicit ordering: this system runs after these (source)
    std::vector<SymbolId> resolved_after_system_ids;  // resolved after: system identities
};

struct DecoratedProgram {
    std::string module_name;  // this program's explicit declaring module name
    // Map key: simple local declaration name. Each declaration still carries a non-empty
    // module_name and canonical_id; later linker/codegen migration tasks will move more
    // consumers to canonical keys while preserving local lookup during semantic analysis.
    std::unordered_map<std::string, ResolvedTrait> traits;
    std::unordered_map<std::string, ResolvedStruct> structs;
    std::unordered_map<std::string, ResolvedEnum> enums;
    std::unordered_map<std::string, ResolvedFunc> funcs;
    std::unordered_set<std::string> pub_templates;
    std::unordered_set<std::string> non_pub_templates;
    std::unordered_set<std::string> pub_events;  // pub event names (for ImportedSymbols export)
    std::vector<SystemDependency> dependency_graph;
    std::vector<ResolvedSourceModule> source_modules;
    StringPool string_pool;
    ProgramNode* ast = nullptr;  // non-owning pointer to original AST
};

// ── Imported Symbols (pub exports from a single module) ────────────────────

/// Canonical identity for an imported system — carries name, canonical_id, and
/// scheduling dependencies needed for `after:` resolution across modules.
struct ImportedSystem {
    std::string name;
    std::string module_name;
    std::string canonical_id;
    std::optional<SymbolId> symbol_id;
    std::vector<std::string> after_systems;  // canonical system IDs this runs after
};

/// Canonical identity for an imported template.
struct ImportedTemplate {
    std::string name;
    std::string module_name;
    std::string canonical_id;
    std::optional<SymbolId> symbol_id;
};

/// Canonical identity for an imported event.
struct ImportedEvent {
    std::string name;
    std::string module_name;
    std::string canonical_id;
    std::optional<SymbolId> symbol_id;
};

/// Canonical identity for an imported function (extern funcs).
struct ImportedFunc {
    std::string name;
    std::string module_name;
    std::string canonical_id;
    std::optional<SymbolId> symbol_id;
};

/// Public symbols extracted from a compiled module artifact.
/// Only `pub`-marked declarations are included.
struct ImportedSymbols {
    std::string module_name;  // qualified name of the source module

    std::unordered_map<std::string, ResolvedTrait> traits;               // pub traits
    std::unordered_map<std::string, ResolvedStruct> structs;             // pub structs
    std::unordered_map<std::string, ResolvedEnum> enums;                 // pub enums
    std::unordered_map<std::string, ResolvedFunc> funcs;                 // pub extern funcs
    std::unordered_map<std::string, ImportedTemplate> templates;         // pub templates with canonical identity
    std::unordered_set<std::string> events;                              // legacy pub event names
    std::unordered_map<std::string, ImportedEvent> event_symbols;        // pub events with canonical identity
    std::unordered_map<std::string, ImportedSystem> systems;             // systems with canonical identity
    std::unordered_map<std::string, ImportedFunc> func_symbols;          // pub funcs with canonical identity
    std::unordered_map<std::string, ImportedTemplate> template_symbols;  // pub templates with canonical identity
};

// ── Module Imports (aggregate for one compilation unit) ────────────────────

/// Aggregate of imported modules' pub symbols for a single compilation unit.
/// Keyed by qualifier: the declared module name or a `use ... as` alias.
struct ModuleImports {
    /// qualifier → pub symbols for that module
    std::unordered_map<std::string, ImportedSymbols> modules;

    /// qualifier → non-pub trait names (for "did you mean to add pub?" errors)
    std::unordered_map<std::string, std::unordered_set<std::string>> non_pub_trait_names;
    std::unordered_map<std::string, std::unordered_set<std::string>> non_pub_template_names;

    /// Global uniqueness index: symbol name → list of qualifiers that export it
    std::unordered_map<std::string, std::vector<std::string>> trait_providers;
    std::unordered_map<std::string, std::vector<std::string>> struct_providers;
    std::unordered_map<std::string, std::vector<std::string>> enum_providers;
    std::unordered_map<std::string, std::vector<std::string>> func_providers;
    std::unordered_map<std::string, std::vector<std::string>> template_providers;
    std::unordered_map<std::string, std::vector<std::string>> event_providers;
    std::unordered_map<std::string, std::vector<std::string>> system_providers;

    [[nodiscard]] bool empty() const {
        return modules.empty();
    }

    /// Add one module's pub symbols under the given qualifier (module name or alias).
    /// non_pub: non-pub trait names in this module, for helpful error diagnostics.
    void add(const std::string& qualifier,
             ImportedSymbols pub_syms,
             std::unordered_set<std::string> non_pub           = {},
             std::unordered_set<std::string> non_pub_templates = {});
};

// ── Unified name resolution (unified-name-resolution change) ────────────────

/// Result of resolving a dotted reference through the unified resolver: the
/// module-scope symbol it names plus any trailing member segments (e.g. the
/// enum member `A` in `inp.Key.A`). The symbol's kind lives on SymbolId and is
/// checked by callers after lookup succeeds.
struct ResolvedRef {
    SymbolId symbol;
    std::vector<std::string> member_segments;
};

// ── Semantic Analyzer ───────────────────────────────────────────────────────

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(ErrorReporter& errors);

    /// Analyze a program with optional multi-module imported symbols.
    /// Omit imports (or pass ModuleImports{}) for a module with no dependencies.
    DecoratedProgram analyze(ProgramNode& program, const ModuleImports& imports = ModuleImports{});

private:
    // Phase 1: Collect type declarations
    void collect_types(ProgramNode& program);
    bool declare_module_scope_symbol(SymbolKind kind, const std::string& name, const SourceLocation& loc);

    // Phase 2: Resolve types in fields
    void resolve_all_types(ProgramNode& program);
    TypeInfo resolve_type_ref(const TypeRef& ref);
    void resolve_trait_references(ProgramNode& program);

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
    void validate_template_use_cycles(ProgramNode& program);
    void flatten_template_compositions(ProgramNode& program);
    void validate_template_backed_entity_overrides(ProgramNode& program);
    void validate_spawn_sites(ProgramNode& program);

    // Hierarchical entity templates (dsl-hierarchical-entity-templates)
    void validate_archetype_template_ref(const std::string& tmpl_ref,
                                         const SourceLocation& location,
                                         const std::string& owner_desc);
    void validate_child_archetypes(const std::vector<ChildArchetypeNode>& children,
                                   const std::string& archetype_kind,
                                   const std::string& archetype_name);
    void validate_child_override_tree(const std::vector<ChildOverrideNode>& overrides,
                                      const std::vector<ChildArchetypeNode>& base_children,
                                      const std::string& site_desc,
                                      bool allow_self);
    void validate_child_required_fields(const std::vector<ChildArchetypeNode>& children,
                                        const std::vector<ChildOverrideNode>& overrides,
                                        const std::string& site_desc,
                                        const SourceLocation& site_loc);
    void validate_hierarchical_entities(ProgramNode& program);
    void validate_stmt_contexts(ProgramNode& program);
    void validate_trait_modifier_rules(ProgramNode& program);
    void validate_exclude_clause(const auto& node);

    // task 11.12: field access not allowed in systems with no filter clause
    void check_no_field_access(const std::vector<std::unique_ptr<StmtNode>>& stmts, const std::string& sys_name);

    // Dynamic ECS helpers
    bool is_trait_declared(const std::string& name) const;
    bool resolve_archetype_template_use(const ArchetypeTemplateUseEntry& use,
                                        const std::string& archetype_kind,
                                        const std::string& archetype_name);
    bool local_non_template_symbol_exists(const std::string& name) const;
    bool imported_non_template_symbol_exists(const std::string& name) const;
    static bool imported_symbols_contain_non_template(const ImportedSymbols& symbols, const std::string& name);
    std::unordered_set<std::string> get_archetype_fields(const std::vector<ArchetypeTraitEntry>& traits) const;
    const ResolvedTrait* find_resolved_trait(const std::string& name) const;
    const ResolvedTrait* find_resolved_trait(const SymbolId& symbol) const;
    const ResolvedTrait* find_resolved_trait(const std::optional<SymbolId>& symbol,
                                             const std::string& fallback_name) const;
    const ResolvedStruct* find_resolved_event(const std::string& name) const;
    TypeInfo infer_expr_type(const ExprNode& expr,
                             const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                             const std::unordered_map<std::string, TypeInfo>& local_bindings,
                             const ResolvedStruct* handler_event) const;
    void validate_spawn_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts, const std::string& context_name);
    void validate_spawn_exprs(const std::vector<std::unique_ptr<StmtNode>>& stmts, const std::string& context_name);
    void validate_spawn_expr(const SpawnExpr& spawn, const SourceLocation& location);
    void validate_context_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                const std::string& context_name,
                                bool in_system_handler);
    void validate_trait_default_values(ProgramNode& program);

    // Phase 4: Build dependency graph
    void build_dependency_graph(ProgramNode& program);
    void collect_system_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts, SystemDependency& dep);

    // Phase 3: std.text.format validation
    bool is_std_text_format_callee(const ExprNode& callee) const;

    // Phase 3: query expression helpers
    std::optional<std::string> get_query_func_name(const ExprNode& callee) const;
    std::optional<std::pair<std::string, std::string>> get_query_module_and_func(const ExprNode& callee) const;
    void validate_query_named_args(const QueryCallExpr& qcall,
                                   const std::string& func_name,
                                   const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                   const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                   const ResolvedStruct* handler_event) const;
    void validate_text_format_calls(ProgramNode& program);
    void validate_text_format_in_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                       const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                       const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                       const ResolvedStruct* handler_event);
    void validate_text_format_in_expr(const ExprNode& expr,
                                      const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                      const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                      const ResolvedStruct* handler_event);
    void validate_one_text_format_call(const CallExpr& call,
                                       const SourceLocation& loc,
                                       const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                       const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                       const ResolvedStruct* handler_event);

    // Phase 5: after: validation
    void validate_after_clauses(ProgramNode& program);

    // Helpers
    bool is_known_type(const std::string& name) const;

    /// Validate one filter entry against local traits and imports.
    /// Sets out_simple_name to the unqualified trait name on success.
    bool resolve_filter_entry(const FilterEntry& entry, std::string& out_simple_name);

    /// Resolve a trait reference (dotted or simple) to its canonical ID.
    /// Reports an error and returns "" on failure.
    std::string resolve_trait_ref_to_canonical(const std::string& ref, const SourceLocation& loc);
    std::optional<SymbolId> try_resolve_trait_ref_to_symbol(const std::string& ref) const;

    /// Resolve an event reference (dotted or simple) to its canonical SymbolId.
    /// Returns nullopt if the event is not found.
    std::optional<SymbolId> try_resolve_event_ref_to_symbol(const std::string& ref) const;

    /// Resolve a system name to its canonical SymbolId.
    std::optional<SymbolId> try_resolve_system_ref_to_symbol(const std::string& ref) const;

    /// Resolve a system name in an after: clause to its canonical SymbolId.
    std::optional<SymbolId> resolve_system_after_ref_to_symbol(
        const std::string& ref,
        const SourceLocation& loc,
        const std::unordered_set<std::string>& local_system_names) const;
    std::string resolve_system_after_ref(const std::string& ref,
                                         const SourceLocation& loc,
                                         const std::unordered_set<std::string>& local_system_names);

    /// Resolve a template/entity reference to its canonical SymbolId.
    std::optional<SymbolId> try_resolve_template_ref_to_symbol(const std::string& ref) const;

    /// Resolve a func reference to its canonical SymbolId.
    std::optional<SymbolId> try_resolve_func_ref_to_symbol(const std::string& ref) const;

    // ── Unified name resolution (design D1/D2/D4) ───────────────────────────
    /// Kind-filtered optional-mode wrapper over resolve_name: the shared
    /// implementation of the per-kind try_resolve_*_to_symbol resolvers
    /// (task 3.1). Trailing member segments mean the reference does not name
    /// a module-scope declaration, so they yield nullopt.
    std::optional<SymbolId> try_resolve_ref_of_kind(const std::string& ref,
                                                    std::initializer_list<SymbolKind> kinds) const;
    /// Resolve dotted segments with fixed precedence: module qualifiers (alias
    /// or canonical module path, longest dotted prefix first), then module-local
    /// declarations, then the std.core prelude. Optional-returning probe form.
    std::optional<ResolvedRef> resolve_name(const std::vector<std::string>& segments) const;
    /// Required form: like resolve_name but reports a diagnostic (with a
    /// qualified-spelling suggestion where possible) when resolution fails.
    std::optional<ResolvedRef> resolve_name_required(const std::vector<std::string>& segments,
                                                     const SourceLocation& loc);
    /// Find an imported module by alias or canonical module path.
    const ImportedSymbols* find_imported_module(const std::string& qualifier_or_canonical) const;
    /// Cross-kind symbol lookup within one imported module's pub exports.
    static std::optional<SymbolId> lookup_imported_symbol(const ImportedSymbols& syms, const std::string& name);
    /// Cross-kind lookup among this module's own declarations.
    std::optional<SymbolId> lookup_local_symbol(const std::string& name) const;
    /// Resolve a call/query callee expression to a func symbol via
    /// resolve_name on its chain segments (design D7). Optional-mode: non-func
    /// results and computed callees yield nullopt.
    std::optional<SymbolId> resolve_callee_symbol(const ExprNode& callee) const;
    /// Enum record lookup by resolved identity (local or imported).
    const ResolvedEnum* find_resolved_enum(const SymbolId& symbol) const;
    /// Resolve/validate an enum member chain on a MemberExpr (design D3).
    void resolve_enum_member_expr(MemberExpr& member, const SourceLocation& loc);
    /// Task 1.5: required-mode validation of input declaration properties.
    void validate_input_decl_props(const InputDeclNode& node);

    ErrorReporter& errors_;
    DecoratedProgram result_;
    ModuleImports imports_;
    bool current_module_is_stdlib_ = false;
    std::string current_module_name_;
    ModuleId current_module_id_;

    // Known type names (populated during Phase 1)
    std::unordered_set<std::string> struct_names_;
    std::unordered_set<std::string> enum_names_;
    std::unordered_set<std::string> trait_names_;
    std::unordered_set<std::string> event_names_;
    std::unordered_set<std::string> func_names_;
    std::unordered_set<std::string> system_names_;
    std::unordered_set<std::string> const_names_;
    std::unordered_map<std::string, SymbolId> module_scope_symbols_;

    // Asset and input declaration tracking (dsl-spec-new-features)
    // Maps identifier name → resolved TypeKind (e.g., "PlayerMesh" → MeshId)
    std::unordered_map<std::string, TypeKind> asset_decl_types_;
    // Maps identifier name → InputButton or InputAxis
    std::unordered_map<std::string, TypeKind> input_decl_types_;

    // For recursion detection
    std::unordered_map<std::string, std::unordered_set<std::string>> call_graph_;

    // ── Dynamic ECS tracking (dynamic-ecs-language change) ──────────────────
    // Separate sets for templates vs entities (spawn only works on templates)
    std::unordered_set<std::string> template_names_;
    std::unordered_set<std::string> entity_names_;

    // Module names/aliases declared via `use` (for `load` reachability check)
    std::unordered_set<std::string> use_names_;

    // Archetype trait entries: archetype_name → nested trait entries list
    std::unordered_map<std::string, const std::vector<ArchetypeTraitEntry>*> archetype_traits_;

    // Flattened child archetype trees: archetype_name → flattened children
    // (dsl-hierarchical-entity-templates)
    std::unordered_map<std::string, const std::vector<ChildArchetypeNode>*> archetype_children_;

    // Template required fields (var with no default and not in config):
    // template_name → set of field names that must be provided at spawn site
    std::unordered_map<std::string, std::unordered_set<std::string>> template_required_fields_;

    // Event declarations as struct-like field maps for emit payload validation.
    std::unordered_map<std::string, ResolvedStruct> event_structs_;
};

}  // namespace cactus