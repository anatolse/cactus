#pragma once

#include "common/source_location.hpp"
#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "frontend/symbol_identity.hpp"

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// Cactus Intermediate Representation, version 1: an owned, pointer-free
/// execution view lowered from a successfully analyzed and linked
/// `DecoratedProgram`. It reuses the frontend's identity and contract value
/// types directly — they are already owned aggregates — and adds only what the
/// execution view needs: stable node identity, rule grouping, typed relations,
/// and canonical ordering.
namespace cactus::cir {

inline constexpr const char* SCHEMA_NAME = "cactus-cir";
inline constexpr int SCHEMA_VERSION      = 1;

/// Prefix reserved for nodes CIR owns rather than the source program, so a
/// synthetic ID can never collide with an authored canonical identity.
inline constexpr const char* SYNTHETIC_PREFIX = "cir:";

/// Stable textual identity of one CIR node. Distinct from `SymbolId` because
/// commit, scheduler-boundary, external-source, and rasterization nodes are not
/// module declarations.
struct CirNodeId {
    std::string value;

    [[nodiscard]] bool empty() const {
        return value.empty();
    }

    friend bool operator==(const CirNodeId&, const CirNodeId&)                  = default;
    friend std::strong_ordering operator<=>(const CirNodeId&, const CirNodeId&) = default;
};

struct CirNodeIdHash {
    [[nodiscard]] std::size_t operator()(const CirNodeId& id) const noexcept {
        return std::hash<std::string>{}(id.value);
    }
};

enum class CirNodeKind : std::uint8_t {
    Handler,
    Phase,
    ExternalEventSource,
    SchedulerBoundary,
    ActivationCommit,
    Rasterization,
};

[[nodiscard]] const char* cir_node_kind_name(CirNodeKind kind);

/// How one endpoint of a schedule dependency touches the trait that caused it.
enum class CirTraitAccessMode : std::uint8_t { Read, Write, Project, Select };

[[nodiscard]] const char* cir_trait_access_mode_name(CirTraitAccessMode mode);

// ── Node identity construction ──────────────────────────────────────────────

[[nodiscard]] CirNodeId handler_node_id(const HandlerIdentity& handler);
[[nodiscard]] CirNodeId phase_node_id(const SymbolId& phase);
[[nodiscard]] CirNodeId event_producer_node_id(CirNodeKind kind, const SymbolId& event);
[[nodiscard]] CirNodeId rasterization_node_id(const SymbolId& phase);

// ── Declarations ────────────────────────────────────────────────────────────

struct CirTraitField {
    std::string name;
    std::string type;
    bool is_var      = false;
    bool is_persist  = false;
    bool is_sync     = false;
    bool has_default = false;
};

struct CirTrait {
    SymbolId symbol;
    std::vector<CirTraitField> fields;
};

/// Presentation and ownership metadata only — never a substitute for the
/// independently schedulable handler nodes it lists.
struct CirRuleGroup {
    SymbolId rule;
    std::vector<CirNodeId> handlers;
};

// ── Nodes ───────────────────────────────────────────────────────────────────

struct CirHandlerNode {
    CirNodeId id;
    SymbolId rule;
    ResolvedHandlerTrigger trigger;
    HandlerImplementationKind implementation = HandlerImplementationKind::Cactus;
    HandlerDomainKind domain                 = HandlerDomainKind::Selectionless;
    std::vector<SymbolId> selection;
    std::vector<SymbolId> exclusion;
    std::vector<RelationBinding> bindings;
    std::optional<SpatialJoinPlan> spatial_join;
    std::vector<SymbolId> reads;
    std::vector<BoundTraitAccess> bound_reads;
    std::vector<SymbolId> writes;
    std::vector<SymbolId> projects;
    std::vector<SymbolId> emits;
    std::vector<InferredHandlerCommand> commands;
    std::vector<std::string> effects;
    std::vector<CirNodeId> explicit_after;
    DeclarationOrder declaration_order;
    SourceLocation location;
};

struct CirPhaseNode {
    CirNodeId id;
    SymbolId phase;
    /// False when another phase's dependency names this phase but its plan
    /// belongs to a module outside this compilation unit.
    bool declared = true;
    std::vector<ResolvedHandlerTrigger> source_dependencies;
    std::optional<SymbolId> runtime_root;
    std::optional<double> every_seconds;
    std::optional<std::int64_t> max_repetitions;
    DeclarationOrder declaration_order;
};

/// Runtime-owned event production: a host external source, a scheduler
/// boot/teardown boundary, or the structural-command commit step.
struct CirEventProducerNode {
    CirNodeId id;
    CirNodeKind kind = CirNodeKind::ExternalEventSource;
    SymbolId event;
};

/// The backend-mediated rasterization step between a render pass's vertex- and
/// fragment-stage handlers.
struct CirRasterizationNode {
    CirNodeId id;
    SymbolId phase;
};

// ── Relations ───────────────────────────────────────────────────────────────

/// Which canonical trait forced an ordering, and how each ordered endpoint
/// touches it. Named for `before`/`after` rather than producer/consumer because
/// explicit order may oppose the natural writer-before-reader direction and
/// write/write pairs are hazards rather than value flow.
struct CirTraitProvenance {
    SymbolId trait;
    std::vector<CirTraitAccessMode> before;
    std::vector<CirTraitAccessMode> after;
};

struct CirScheduleRelation {
    CirNodeId before;
    CirNodeId after;
    ScheduleEdgeKind kind               = ScheduleEdgeKind::DataConflict;
    ScheduleEdgeOrientation orientation = ScheduleEdgeOrientation::DeclarationOrder;
    std::vector<CirTraitProvenance> trait_provenance;
    std::vector<std::string> effect_provenance;
};

struct CirPhaseDependencyRelation {
    CirNodeId upstream;
    CirNodeId downstream;
};

struct CirPhaseBarrierRelation {
    CirNodeId upstream_phase;
    CirNodeId downstream_handler;
};

/// May legally participate in cycles; never part of schedule validation.
struct CirEventFlowRelation {
    CirNodeId producer;
    SymbolId event;
    CirNodeId consumer;
};

struct CirRenderFlowRelation {
    CirNodeId before;
    CirNodeId after;
};

// ── Activation schedules ────────────────────────────────────────────────────

struct CirDependencyLevel {
    std::uint64_t index = 0;
    std::vector<CirNodeId> handlers;
};

struct CirActivationSchedule {
    ResolvedHandlerTrigger activation;
    std::vector<CirNodeId> stable_order;
    std::vector<CirDependencyLevel> levels;
};

// ── Program ─────────────────────────────────────────────────────────────────

struct CirProgram {
    std::string schema = SCHEMA_NAME;
    int version        = SCHEMA_VERSION;
    std::vector<std::string> modules;
    std::vector<CirTrait> traits;
    std::vector<CirRuleGroup> rule_groups;
    std::vector<CirHandlerNode> handlers;
    std::vector<CirPhaseNode> phases;
    std::vector<CirEventProducerNode> event_producers;
    std::vector<CirRasterizationNode> rasterizations;
    std::vector<CirScheduleRelation> schedule_dependencies;
    std::vector<CirPhaseDependencyRelation> phase_dependencies;
    std::vector<CirPhaseBarrierRelation> phase_barriers;
    std::vector<CirEventFlowRelation> event_flows;
    std::vector<CirRenderFlowRelation> render_pass_flows;
    std::vector<CirActivationSchedule> activation_schedules;
};

// ── Canonical ordering ──────────────────────────────────────────────────────
// Defined once here so lowering, validation, and the serializers cannot drift
// on what "canonical order" means.

[[nodiscard]] bool symbol_precedes(const SymbolId& left, const SymbolId& right);
[[nodiscard]] bool trigger_precedes(const ResolvedHandlerTrigger& left, const ResolvedHandlerTrigger& right);
[[nodiscard]] bool schedule_relation_precedes(const CirScheduleRelation& left, const CirScheduleRelation& right);
[[nodiscard]] bool phase_dependency_precedes(const CirPhaseDependencyRelation& left,
                                             const CirPhaseDependencyRelation& right);
[[nodiscard]] bool phase_barrier_precedes(const CirPhaseBarrierRelation& left, const CirPhaseBarrierRelation& right);
[[nodiscard]] bool event_flow_precedes(const CirEventFlowRelation& left, const CirEventFlowRelation& right);
[[nodiscard]] bool render_flow_precedes(const CirRenderFlowRelation& left, const CirRenderFlowRelation& right);

}  // namespace cactus::cir
