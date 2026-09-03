#include "cir/cir.hpp"

#include <string>
#include <utility>

namespace cactus::cir {

const char* cir_node_kind_name(CirNodeKind kind) {
    switch (kind) {
        case CirNodeKind::Handler:
            return "handler";
        case CirNodeKind::Phase:
            return "phase";
        case CirNodeKind::ExternalEventSource:
            return "external-event-source";
        case CirNodeKind::SchedulerBoundary:
            return "scheduler-boundary";
        case CirNodeKind::ActivationCommit:
            return "activation-commit";
        case CirNodeKind::Rasterization:
            return "rasterization";
    }
    std::unreachable();
}

const char* cir_trait_access_mode_name(CirTraitAccessMode mode) {
    switch (mode) {
        case CirTraitAccessMode::Read:
            return "read";
        case CirTraitAccessMode::Write:
            return "write";
        case CirTraitAccessMode::Project:
            return "project";
        case CirTraitAccessMode::Select:
            return "select";
    }
    std::unreachable();
}

CirNodeId handler_node_id(const HandlerIdentity& handler) {
    return CirNodeId{.value = handler.canonical_id()};
}

CirNodeId phase_node_id(const SymbolId& phase) {
    return CirNodeId{.value = std::string{SYNTHETIC_PREFIX} + "phase/" + make_canonical_id(phase)};
}

CirNodeId event_producer_node_id(CirNodeKind kind, const SymbolId& event) {
    return CirNodeId{.value =
                         std::string{SYNTHETIC_PREFIX} + cir_node_kind_name(kind) + "/" + make_canonical_id(event)};
}

CirNodeId rasterization_node_id(const SymbolId& phase) {
    return CirNodeId{.value = std::string{SYNTHETIC_PREFIX} + "rasterization/" + make_canonical_id(phase)};
}

bool symbol_precedes(const SymbolId& left, const SymbolId& right) {
    const auto lhs = make_canonical_id(left);
    const auto rhs = make_canonical_id(right);
    if (lhs != rhs) {
        return lhs < rhs;
    }
    return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
}

bool trigger_precedes(const ResolvedHandlerTrigger& left, const ResolvedHandlerTrigger& right) {
    if (left.kind != right.kind) {
        return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
    }
    return symbol_precedes(left.symbol, right.symbol);
}

bool schedule_relation_precedes(const CirScheduleRelation& left, const CirScheduleRelation& right) {
    if (left.before != right.before) {
        return left.before < right.before;
    }
    if (left.after != right.after) {
        return left.after < right.after;
    }
    if (left.kind != right.kind) {
        return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
    }
    return static_cast<std::uint8_t>(left.orientation) < static_cast<std::uint8_t>(right.orientation);
}

bool phase_dependency_precedes(const CirPhaseDependencyRelation& left, const CirPhaseDependencyRelation& right) {
    if (left.upstream != right.upstream) {
        return left.upstream < right.upstream;
    }
    return left.downstream < right.downstream;
}

bool phase_barrier_precedes(const CirPhaseBarrierRelation& left, const CirPhaseBarrierRelation& right) {
    if (left.upstream_phase != right.upstream_phase) {
        return left.upstream_phase < right.upstream_phase;
    }
    return left.downstream_handler < right.downstream_handler;
}

bool event_flow_precedes(const CirEventFlowRelation& left, const CirEventFlowRelation& right) {
    if (left.producer != right.producer) {
        return left.producer < right.producer;
    }
    if (left.event != right.event) {
        return symbol_precedes(left.event, right.event);
    }
    return left.consumer < right.consumer;
}

bool render_flow_precedes(const CirRenderFlowRelation& left, const CirRenderFlowRelation& right) {
    if (left.before != right.before) {
        return left.before < right.before;
    }
    return left.after < right.after;
}

}  // namespace cactus::cir
