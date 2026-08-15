#pragma once

#include "common/error_reporter.hpp"
#include "common/string_pool.hpp"
#include "common/types.hpp"
#include "frontend/ast.hpp"
#include "frontend/symbol_identity.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

// ── Decorated AST (resolved output of the semantic analyzer) ───────────────

// Resolved binding for a declared phase field's initializer (e.g. `dt: float = frame.dt`).
// Records where codegen should read the value from at runtime, without re-resolving
// the original member-chain expression.
struct PhaseFieldSource {
    enum class Kind : std::uint8_t { RootEvent, UpstreamPhase };
    Kind kind = Kind::RootEvent;
    SymbolId source;     // the runtime root event or upstream phase this field reads from
    std::string member;  // field name on that source
};

struct ResolvedField {
    std::string name;
    TypeInfo type;
    bool is_let             = false;
    bool is_var             = false;
    bool is_persist         = false;
    bool is_sync            = false;
    bool is_pub             = false;
    bool has_default        = false;
    bool is_synthesized     = false;
    bool is_completion_only = false;
    std::optional<PhaseFieldSource> source_binding;
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
    // nullopt means an extern binding has no known summary and must be treated
    // conservatively as `external`; an empty set is an explicitly pure binding.
    std::optional<std::unordered_set<std::string>> effect_summary;
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

struct ResolvedEvent : CanonicalIdentity {
    std::string name;
    std::vector<ResolvedField> fields;
    bool is_pub      = false;
    bool is_external = false;
};

struct ResolvedPhase : CanonicalIdentity {
    std::string name;
    std::vector<ResolvedField> fields;
    std::vector<ResolvedHandlerTrigger> from_sources;
    std::vector<ResolvedHandlerTrigger> after_phases;
    std::vector<SymbolId> upstream_phases;
    std::optional<SymbolId> runtime_root;
    std::optional<double> every_seconds;
    std::optional<std::int64_t> max_repetitions;
    bool is_pub    = false;
    bool has_every = false;
    bool has_max   = false;
};

/// Codegen-facing source module view. The AST remains the parsed syntax tree,
/// but semantic analysis mutates its module-scope reference sites with resolved
/// SymbolIds. Linkers preserve these per-module views instead of fabricating a
/// combined raw AST.
struct ResolvedSourceModule {
    std::string module_name;
    ProgramNode* ast = nullptr;  // non-owning; owned by the CLI/test pipeline
};

struct RuleDependency {
    std::string rule_name;
    std::optional<SymbolId> rule_id;  // resolved rule identity
    std::unordered_set<std::string> reads;
    std::unordered_set<std::string> writes;
    std::unordered_set<std::string> emits;
    std::vector<std::string> after_rules;           // explicit ordering: this rule runs after these (source)
    std::vector<SymbolId> resolved_after_rule_ids;  // resolved after: rule identities
};

struct InferredHandlerCommand {
    HandlerCommandKind kind = HandlerCommandKind::Destroy;
    std::optional<SymbolId> target;

    friend bool operator==(const InferredHandlerCommand&, const InferredHandlerCommand&) = default;
};

/// Canonical identity of one executable handler. A handler is identified by
/// its owning rule and resolved trigger rather than by source spelling.
struct HandlerIdentity {
    SymbolId rule;
    ResolvedHandlerTrigger trigger;

    [[nodiscard]] std::string canonical_id() const {
        return make_canonical_id(rule) + "/on " + make_canonical_id(trigger.symbol);
    }

    friend bool operator==(const HandlerIdentity&, const HandlerIdentity&) = default;
};

struct HandlerIdentityHash {
    [[nodiscard]] std::size_t operator()(const HandlerIdentity& handler) const noexcept {
        std::size_t seed = SymbolIdHash{}(handler.rule);
        seed ^= SymbolIdHash{}(handler.trigger.symbol) + 0x9E3779B9U + (seed << 6U) + (seed >> 2U);
        seed ^= std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(handler.trigger.kind)) + 0x9E3779B9U +
                (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

/// Stable source/link order. ProgramLinker assigns module_index while the
/// frontend records declaration and handler indices from source order.
struct DeclarationOrder {
    std::uint64_t module_index      = 0;
    std::uint64_t declaration_index = 0;
    std::uint64_t handler_index     = 0;

    friend bool operator==(const DeclarationOrder&, const DeclarationOrder&) = default;
};

// ── Pair relations (dsl-pair-relations) ─────────────────────────────────────

enum class HandlerDomainKind : std::uint8_t { Selectionless, Unary, Pair };

// One named, ordered binding within a pair handler's relation, carrying the
// canonical identities of the traits it requires.
struct RelationBinding {
    std::string name;
    std::vector<SymbolId> required_traits;

    friend bool operator==(const RelationBinding&, const RelationBinding&) = default;
};

// A single trait read qualified by which pair binding it was read through
// (e.g. `body.Collider` vs `wall.Collider` both read canonical Collider, but
// remain distinguishable for future relation-aware scheduling).
struct BoundTraitAccess {
    std::size_t binding_index = 0;
    SymbolId trait;

    friend bool operator==(const BoundTraitAccess&, const BoundTraitAccess&) = default;
};

// ── Spatial join recognition (spatial-broadphase-runtime, dsl-where-clause) ──

enum class SpatialJoinDimension : std::uint8_t { Flat2D, Volume3D };

// Where a pair binding's position/radius value lives: the trait it was read
// through, plus any remaining field segments after that trait access (e.g.
// `a.transform.position` -> trait `WorldTransform`, field path ["position"]).
struct SpatialJoinAccess {
    SymbolId trait;
    std::vector<std::string> field_path;

    friend bool operator==(const SpatialJoinAccess&, const SpatialJoinAccess&) = default;
};

// One pair binding's role in a recognized spatial predicate.
struct SpatialJoinBinding {
    std::size_t binding_index = 0;
    SpatialJoinAccess position;
    SpatialJoinAccess radius;

    friend bool operator==(const SpatialJoinBinding&, const SpatialJoinBinding&) = default;
};

// Populated only when semantic analysis recognizes a pair rule's `where:`
// predicate list as containing a direct, unwrapped call to
// std.collision.flat.circles_overlap / std.collision.volume.spheres_overlap
// with binding-rooted position/radius arguments, and both pair bindings
// require identical trait sets (spatial-broadphase-runtime, dsl-where-clause).
// `left` supplies the call's first two arguments, `right` its last two —
// codegen resolves which of `HandlerContract::pair_bindings` each refers to
// via `binding_index`, independent of the bindings' declaration order.
struct SpatialJoinPlan {
    SpatialJoinDimension dimension = SpatialJoinDimension::Flat2D;
    SpatialJoinBinding left;
    SpatialJoinBinding right;
    // Index into the owning rule's `where_clause->predicates` naming the
    // recognized call, so codegen can exclude it from the generic residual
    // guard it synthesizes for every other predicate (it is instead
    // re-verified directly against SAP's candidates, since broad-phase
    // overlap is conservative and not itself proof of exact overlap).
    std::size_t matched_predicate_index = 0;

    friend bool operator==(const SpatialJoinPlan&, const SpatialJoinPlan&) = default;
};

struct HandlerContract {
    HandlerDomainKind domain_kind = HandlerDomainKind::Selectionless;
    std::vector<SymbolId> selection;             // Unary domain: positive traits
    std::vector<SymbolId> exclusion;             // Unary domain: excluded traits
    std::vector<RelationBinding> pair_bindings;  // Pair domain: exactly two, source order
    // Pair domain only: set when a recognized, SAP-eligible spatial predicate
    // was found in the rule's `where:` clause (spatial-broadphase-runtime).
    std::optional<SpatialJoinPlan> spatial_join;

    [[nodiscard]] bool is_selectionless() const {
        return domain_kind == HandlerDomainKind::Selectionless;
    }

    std::unordered_set<SymbolId> reads;         // conservative canonical read union
    std::vector<BoundTraitAccess> bound_reads;  // precise pair binding + trait reads
    std::unordered_set<SymbolId> writes;        // durable trait writes
    std::unordered_set<SymbolId> projects;      // projected (transient) trait outputs
    std::unordered_set<SymbolId> emits;
    std::vector<InferredHandlerCommand> commands;
    std::unordered_set<std::string> effects;

    // Conflict detection treats a projected trait as production of that trait
    // for ordering purposes, same as a durable write, without collapsing the
    // distinct `writes`/`projects` contract capabilities themselves.
    [[nodiscard]] std::unordered_set<SymbolId> produced_traits() const {
        std::unordered_set<SymbolId> produced = writes;
        produced.insert(projects.begin(), projects.end());
        return produced;
    }
};

/// Transitional inference result retained for focused semantic tests and old
/// consumers while HandlerNode becomes the authoritative execution record.
struct InferredHandlerContract : HandlerContract {
    SymbolId rule;
    ResolvedHandlerTrigger trigger;
};

enum class HandlerImplementationKind : std::uint8_t { Cactus, External };

struct HandlerNode {
    HandlerIdentity identity;
    HandlerImplementationKind implementation = HandlerImplementationKind::Cactus;
    HandlerContract contract;
    std::vector<HandlerIdentity> explicit_after;
    DeclarationOrder declaration_order;
    SourceLocation location;
};

// Precomputes each handler's produced_traits() once, indexed the same as
// `handlers`, so O(handlers^2) conflict-detection passes don't recompute it
// per pair.
[[nodiscard]] inline std::vector<std::unordered_set<SymbolId>> precompute_produced_by_handler(
    const std::vector<HandlerNode>& handlers) {
    std::vector<std::unordered_set<SymbolId>> produced_by_handler;
    produced_by_handler.reserve(handlers.size());
    for (const auto& handler : handlers) {
        produced_by_handler.push_back(handler.contract.produced_traits());
    }
    return produced_by_handler;
}

struct PhasePlan {
    SymbolId phase;
    std::vector<ResolvedHandlerTrigger> source_dependencies;
    std::vector<SymbolId> completion_dependencies;
    std::vector<ResolvedField> fields;
    std::optional<SymbolId> runtime_root;
    std::optional<double> every_seconds;
    std::optional<std::int64_t> max_repetitions;
    DeclarationOrder declaration_order;
};

enum class ScheduleEdgeKind : std::uint8_t { ExplicitHandler, ExplicitRule, DataConflict, EffectConflict };
enum class ScheduleEdgeOrientation : std::uint8_t { Explicit, WriterBeforeReader, DeclarationOrder };

struct ScheduleEdge {
    HandlerIdentity before;
    HandlerIdentity after;
    ScheduleEdgeKind kind               = ScheduleEdgeKind::DataConflict;
    ScheduleEdgeOrientation orientation = ScheduleEdgeOrientation::DeclarationOrder;
    std::vector<SymbolId> trait_provenance;
    std::vector<std::string> effect_provenance;
};

struct PhaseBarrierEdge {
    SymbolId upstream_phase;
    HandlerIdentity downstream_handler;
};

struct EventFlowEdge {
    HandlerIdentity producer;
    SymbolId event;
    HandlerIdentity consumer;
};

struct DependencyLevel {
    ResolvedHandlerTrigger activation;
    std::uint64_t index = 0;
    std::vector<HandlerIdentity> handlers;
};

struct ExecutionGraph {
    std::vector<PhasePlan> phases;
    std::vector<HandlerNode> handlers;
    std::vector<ScheduleEdge> schedule_edges;
    std::vector<PhaseBarrierEdge> phase_barriers;
    std::vector<EventFlowEdge> event_flows;
    std::vector<HandlerIdentity> stable_topological_order;
    std::vector<DependencyLevel> dependency_levels;
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
    std::unordered_map<std::string, ResolvedEvent> events;
    std::unordered_map<std::string, ResolvedPhase> phases;
    std::unordered_set<std::string> pub_templates;
    std::unordered_set<std::string> non_pub_templates;
    std::unordered_set<std::string> pub_events;  // pub event names (for ImportedSymbols export)
    std::vector<RuleDependency> dependency_graph;
    std::vector<InferredHandlerContract> handler_contracts;
    ExecutionGraph execution_graph;
    std::vector<ResolvedSourceModule> source_modules;
    StringPool string_pool;
    ProgramNode* ast = nullptr;  // non-owning pointer to original AST
};

// ── Imported Symbols (pub exports from a single module) ────────────────────

/// Canonical identity for an imported rule — carries name, canonical_id, and
/// scheduling dependencies needed for `after:` resolution across modules.
struct ImportedRule {
    std::string name;
    std::string module_name;
    std::string canonical_id;
    std::optional<SymbolId> symbol_id;
    std::vector<std::string> after_rules;  // canonical rule IDs this runs after
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
    std::vector<ResolvedField> fields;
    bool is_external = false;
};

/// Canonical identity and public metadata for an imported phase.
struct ImportedPhase {
    std::string name;
    std::string module_name;
    std::string canonical_id;
    std::optional<SymbolId> symbol_id;
    std::vector<ResolvedField> fields;
    std::vector<SymbolId> upstream_phases;
    std::optional<SymbolId> runtime_root;
    std::optional<double> every_seconds;
    std::optional<std::int64_t> max_repetitions;
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
    std::unordered_map<std::string, ImportedPhase> phase_symbols;        // pub phases with canonical identity
    std::unordered_map<std::string, ImportedRule> rules;                 // rules with canonical identity
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
    std::unordered_map<std::string, std::vector<std::string>> phase_providers;
    std::unordered_map<std::string, std::vector<std::string>> rule_providers;

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

// Resolved binding-relative trait namespace for one pair binding, built once
// per rule (from its `pairs:` clause) and reused across that rule's
// handlers while typing and validating handler bodies. Keys are the dotted
// access spelling used at that binding: a bare local trait name ("Transform"),
// an imported module-qualified name ("tf.WorldTransform"), or a binding-local
// alias ("transform") — mirroring FilterEntry's dotted-name/alias shape.
struct PairBindingScope {
    std::size_t index = 0;
    std::unordered_map<std::string, SymbolId> trait_by_access_key;
};

// binding identifier (e.g. "body") -> its resolved trait namespace.
using PairScope = std::unordered_map<std::string, PairBindingScope>;

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

    // dsl-where-clause: runs before type/trait resolution. Desugars each
    // where:-bearing rule's predicate list into an equivalent
    // `if not (pred_1 and pred_2 and ...): return` guard, cloned and
    // prepended to every one of that rule's handler bodies. This is the only
    // place where: is lowered — codegen and contract inference never learn
    // it exists; they see an ordinary leading guard statement, identical to
    // one an author could have written by hand (see design.md).
    static void desugar_where_clauses(ProgramNode& program);

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
    // The 5 sequential phases of validate_phase_declarations, in order: collect
    // phases/constants; validate from:/after:/every:/max:; DFS lineage+cycle
    // detection; synthesize dt/alpha fields; validate+bind field initializers.
    struct PhaseCollection {
        std::unordered_map<std::string, PhaseNode*> local_phases;
        std::vector<std::string> phase_order;
        std::unordered_map<std::string, const ExprNode*> constants;
    };
    [[nodiscard]] static PhaseCollection collect_phase_declarations(ProgramNode& program);
    void validate_phase_from_after_every_max(const PhaseCollection& phases);
    void resolve_phase_lineage(const PhaseCollection& phases);
    void synthesize_phase_periodic_fields(const PhaseCollection& phases);
    void validate_phase_field_initializers(const PhaseCollection& phases);
    void validate_phase_declarations(ProgramNode& program);
    void validate_rule_filters(ProgramNode& program);
    void validate_pair_bindings(RuleNode& rule);
    [[nodiscard]] static PairScope build_pair_scope(const PairClause& pairs);
    // dsl-where-clause: requires an existing filter:/pairs: domain, type-checks
    // every predicate as bool, and enforces purity via check_where_purity_expr
    // — the same recursive deny-list shape as check_func_purity_expr, reused
    // for a predicate-expression list rather than a func's statement body.
    void validate_where_clauses(ProgramNode& program);
    void check_where_purity_expr(const ExprNode& expr);
    void validate_external_handler_contracts(ProgramNode& program);
    void validateOrderByClause(const RuleNode& rule);
    void validateOrderByClause(const ExternRuleNode& rule);
    void validate_event_usage(ProgramNode& program);
    void validate_event_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                              const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                              const std::unordered_map<std::string, TypeInfo>& local_bindings,
                              const ResolvedStruct* handler_event,
                              const std::string& rule_name,
                              const PairScope* pair_scope = nullptr);
    void validate_trait_match_stmt(const TraitMatchStmt& stmt,
                                   const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                   const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                   const ResolvedStruct* handler_event,
                                   const std::string& rule_name,
                                   bool in_rule_handler,
                                   const PairScope* pair_scope = nullptr);

    // Shared by the 6 trait-override-assignment validation sites (template/entity
    // trait blocks, child archetypes, child overrides, template-backed entity
    // overrides, spawn statements, spawn expressions): given entries already known
    // to target a valid trait on their override site (or, when report_unknown_trait
    // is true, entries whose trait validity this call itself should check),
    // validates each assignment's field against the resolved trait and, when
    // check_self is set, rejects `self` usage. When provided is non-null, every
    // assigned field name is also recorded there for the caller's required-field
    // coverage check.
    void validate_trait_override_assignments(const std::vector<const ArchetypeTraitEntry*>& entries,
                                             const std::string& context_desc,
                                             bool check_self,
                                             bool report_unknown_trait,
                                             std::unordered_set<std::string>* provided = nullptr);

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

    // task 11.12: field access not allowed in rules with no filter clause
    void check_no_field_access(const std::vector<std::unique_ptr<StmtNode>>& stmts, const std::string& rule_name);

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
    const ResolvedFunc* find_resolved_func(const SymbolId& symbol) const;
    const ResolvedStruct* find_resolved_event(const std::string& name) const;

    /// Shared by validateOrderByClause, validate_event_usage, and
    /// validate_text_format_calls: resolves a filter: clause's rich entries
    /// and backward-compat trait_names into a name/alias -> ResolvedTrait map.
    [[nodiscard]] std::unordered_map<std::string, const ResolvedTrait*> build_filter_bindings(
        const FilterClause& filter) const;
    // Shared by validateOrderByClause(RuleNode)/validateOrderByClause(ExternRuleNode):
    // resolves one order-by key's dotted field path against its filter alias's
    // trait, reporting "not declared"/"not a valid field"/"not scalar-comparable"
    // as appropriate.
    void validate_order_by_key(const SortKey& key,
                               const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings);
    TypeInfo infer_expr_type(const ExprNode& expr,
                             const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                             const std::unordered_map<std::string, TypeInfo>& local_bindings,
                             const ResolvedStruct* handler_event,
                             const PairScope* pair_scope = nullptr) const;
    // infer_expr_type's IdentExpr and MemberExpr arms, its two largest and
    // most branching cases.
    TypeInfo infer_ident_expr_type(const IdentExpr& ident,
                                   const SourceLocation& location,
                                   const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                   const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                   const PairScope* pair_scope) const;
    TypeInfo infer_member_expr_type(const MemberExpr& member,
                                    const SourceLocation& location,
                                    const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                    const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                    const ResolvedStruct* handler_event,
                                    const PairScope* pair_scope) const;
    // Validates vec2(...)/vec3(...) constructor calls (1-argument splat or
    // 2-/3-argument component form, each argument float-typed) from
    // infer_expr_type's CallExpr arm. Returns nullopt when the callee isn't
    // vec2/vec3, so the caller falls through to its other CallExpr handling.
    std::optional<TypeInfo> infer_vector_constructor_call_type(
        const CallExpr& call,
        const SourceLocation& location,
        const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
        const std::unordered_map<std::string, TypeInfo>& local_bindings,
        const ResolvedStruct* handler_event,
        const PairScope* pair_scope) const;

    // Recognizes a ForeachStmt::iterable that is the range(begin, end, step=1)
    // intrinsic: a bare-identifier call, never a real function. When it matches,
    // validates the argument count (2 or 3) and that each argument is int-typed,
    // then returns true so the caller types the loop variable as int directly
    // instead of taking the generic list[T] iterable path. Returns false (no
    // validation performed) when iterable isn't a range(...) call, so the caller
    // falls through to the existing list[T] handling.
    bool validate_range_iterable(const ExprNode& iterable,
                                 const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                 const std::unordered_map<std::string, TypeInfo>& local_bindings,
                                 const ResolvedStruct* handler_event,
                                 const PairScope* pair_scope) const;

    /// Shared by validate_event_stmts's 4 command lambdas and
    /// validate_context_stmts's 3 command arms: if target_expr is present,
    /// infers its type and reports wrong_type_message unless it's entity_id
    /// (or still-unknown). No-op when target_expr is absent — callers that
    /// require an explicit target (e.g. inside a pair handler) check that
    /// themselves first.
    void require_optional_entity_id_target(const std::optional<std::unique_ptr<ExprNode>>& target_expr,
                                           const SourceLocation& location,
                                           const std::string& wrong_type_message,
                                           const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                           const std::unordered_map<std::string, TypeInfo>& locals,
                                           const ResolvedStruct* handler_event,
                                           const PairScope* pair_scope = nullptr);

    /// Shared by validate_event_stmts's validate_add/validate_project lambdas:
    /// validates a trait's supplied field arguments (unknown-field detection,
    /// per-field type mismatch) and required-field coverage, reporting
    /// "... in <context_desc>" (e.g. "`add Foo`") for each kind of failure.
    void validate_trait_field_supply(const ResolvedTrait& trait,
                                     const std::vector<FieldAssignment>& args,
                                     const std::string& context_desc,
                                     const SourceLocation& required_field_location,
                                     const std::unordered_map<std::string, const ResolvedTrait*>& filter_bindings,
                                     const std::unordered_map<std::string, TypeInfo>& locals,
                                     const ResolvedStruct* handler_event,
                                     const PairScope* pair_scope);

    // Resolved binding + canonical trait identity for a pair member-access
    // chain (e.g. `body.tf.WorldTransform.position`), plus how many leading
    // dotted segments (after the binding) named the trait itself — the
    // remainder is an ordinary field/aggregate-member path on that trait.
    struct PairMemberResolution {
        std::size_t binding_index = 0;
        SymbolId trait_id;
        std::size_t consumed_segments = 0;
    };
    [[nodiscard]] static std::optional<PairMemberResolution> resolve_pair_member_chain(
        const std::string& binding_name,
        const std::vector<std::string>& segments,
        const PairScope& pair_scope);
    InferredHandlerContract infer_pair_handler_contract(const RuleNode& rule,
                                                        const EventHandlerNode& handler,
                                                        const PairScope& pair_scope) const;
    // dsl-where-clause / spatial-broadphase-runtime: read-only pattern-match
    // over an already-validated `where:` predicate list for a direct,
    // unwrapped circles_overlap/spheres_overlap call with binding-rooted
    // position/radius arguments, eligible only when both pair bindings
    // require identical trait sets. Never reports a diagnostic of its own —
    // every non-matching shape simply yields nullopt (the predicate remains
    // an ordinary residual predicate).
    [[nodiscard]] static std::optional<SpatialJoinPlan> recognize_spatial_join(const RuleNode& rule,
                                                                                const PairScope& pair_scope);

    // A resolved spatial-predicate argument: the pair binding it's rooted at
    // (for the same-binding/distinct-bindings checks in
    // try_recognize_spatial_predicate below), plus where within that
    // binding's trait namespace the value lives.
    struct SpatialJoinResolvedArg {
        std::string binding_name;
        SpatialJoinAccess access;
        std::size_t binding_index = 0;
    };
    [[nodiscard]] static std::optional<SpatialJoinResolvedArg> resolve_spatial_join_arg(const ExprNode& arg,
                                                                                        const PairScope& pair_scope);
    // One `where:` predicate's recognition attempt: matches recognize_spatial_join's
    // shape/eligibility checks against a single call, independent of its
    // position in the predicate list (recognize_spatial_join fills in
    // matched_predicate_index once a match is found).
    struct SpatialJoinMatch {
        SpatialJoinDimension dimension = SpatialJoinDimension::Flat2D;
        SpatialJoinBinding left;
        SpatialJoinBinding right;
    };
    [[nodiscard]] static std::optional<SpatialJoinMatch> try_recognize_spatial_predicate(const CallExpr& call,
                                                                                         const PairScope& pair_scope);
    void validate_spawn_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts, const std::string& context_name);
    void validate_spawn_exprs(const std::vector<std::unique_ptr<StmtNode>>& stmts, const std::string& context_name);
    void validate_spawn_expr(const SpawnExpr& spawn, const SourceLocation& location);
    void validate_context_stmts(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                const std::string& context_name,
                                bool in_rule_handler);
    void validate_trait_default_values(ProgramNode& program);

    // Phase 4: Build dependency graph
    void build_dependency_graph(ProgramNode& program);
    // build_dependency_graph's 3 per-declaration-kind branches, one per
    // Declaration alternative it handles.
    void collect_phase_plan(const PhaseNode& phase, std::size_t declaration_index);
    void collect_rule_dependency(const RuleNode& rule, std::size_t declaration_index);
    void collect_extern_rule_dependency(const ExternRuleNode& rule, std::size_t declaration_index);
    void collect_rule_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts, RuleDependency& dep);

    using LocalNames = std::unordered_set<std::string>;

    // Shared AST-walking core for infer_regular_handler_contract and
    // infer_pair_handler_contract: walks a handler body accumulating
    // commands/effects/emits/reads-via-TraitMatchStmt identically for both,
    // delegating the two points where the callers genuinely differ — how a
    // read resolves off an identifier/member-chain expression, and what a
    // `VarAssign`/`project` statement does — to the supplied hooks.
    // resolve_read(expr, locals) is checked first in visit_expr; returning
    // true means "fully handled, stop" (matching each caller's own
    // short-circuit shape), false falls through to the shared per-kind walk
    // (where IdentExpr is a no-op and MemberExpr falls back to visiting the
    // object). handle_var_assign(node, locals) runs after node.value has
    // already been visited for reads. on_project_trait(trait) runs for a
    // resolved `project` statement's trait id.
    void walk_handler_body(const std::vector<std::unique_ptr<StmtNode>>& body,
                           LocalNames handler_locals,
                           InferredHandlerContract& contract,
                           const std::function<bool(const ExprNode&, const LocalNames&)>& resolve_read,
                           const std::function<void(const VarAssign&, const LocalNames&)>& handle_var_assign,
                           const std::function<void(const SymbolId&)>& on_project_trait) const;

    InferredHandlerContract infer_regular_handler_contract(const RuleNode& rule, const EventHandlerNode& handler) const;

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

    /// Shared by validate_rule_filters's RuleNode/ExternRuleNode branches:
    /// validates a filter: clause's traits (rich entries, or the
    /// backward-compat simple trait_names list), reporting "<owner_desc>
    /// filters on unknown trait '...'" for any that don't resolve.
    void validate_filter_clause_traits(FilterClause& filter, const std::string& owner_desc);

    /// Resolve a trait reference (dotted or simple) to its canonical ID.
    /// Reports an error and returns "" on failure.
    std::string resolve_trait_ref_to_canonical(const std::string& ref, const SourceLocation& loc);
    std::optional<SymbolId> try_resolve_trait_ref_to_symbol(const std::string& ref) const;

    /// Resolve an event reference (dotted or simple) to its canonical SymbolId.
    /// Returns nullopt if the event is not found.
    std::optional<SymbolId> try_resolve_event_ref_to_symbol(const std::string& ref) const;

    /// Resolve a phase reference (dotted or simple) to its canonical SymbolId.
    std::optional<SymbolId> try_resolve_phase_ref_to_symbol(const std::string& ref) const;

    /// Resolve a regular or external handler trigger and preserve its semantic kind.
    std::optional<ResolvedHandlerTrigger> try_resolve_handler_trigger(const std::string& ref) const;

    /// Return whether a canonical event symbol has runtime-only external provenance.
    [[nodiscard]] bool is_external_event(const SymbolId& symbol) const;

    /// Return phase-analysis metadata by canonical identity.
    [[nodiscard]] const std::vector<ResolvedField>* find_event_fields(const SymbolId& symbol) const;
    [[nodiscard]] const ResolvedPhase* find_local_phase(const SymbolId& symbol) const;
    [[nodiscard]] const ImportedPhase* find_imported_phase(const SymbolId& symbol) const;
    [[nodiscard]] const std::vector<ResolvedField>* find_phase_fields(const SymbolId& symbol) const;

    /// Resolve a rule name to its canonical SymbolId.
    std::optional<SymbolId> try_resolve_rule_ref_to_symbol(const std::string& ref) const;

    /// Resolve a rule name in an after: clause to its canonical SymbolId.
    std::optional<SymbolId> resolve_rule_after_ref_to_symbol(
        const std::string& ref,
        const SourceLocation& loc,
        const std::unordered_set<std::string>& local_rule_names) const;
    std::string resolve_rule_after_ref(const std::string& ref,
                                       const SourceLocation& loc,
                                       const std::unordered_set<std::string>& local_rule_names);

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
    std::unordered_set<std::string> phase_names_;
    std::unordered_set<std::string> func_names_;
    std::unordered_set<std::string> rule_names_;
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