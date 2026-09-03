#include "cir/cir_lowering.hpp"

#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cactus::cir {
namespace {

std::vector<SymbolId> sorted_symbols(const std::unordered_set<SymbolId>& symbols) {
    std::vector<SymbolId> sorted(symbols.begin(), symbols.end());
    std::ranges::sort(sorted, symbol_precedes);
    return sorted;
}

std::vector<std::string> sorted_strings(const std::unordered_set<std::string>& values) {
    std::vector<std::string> sorted(values.begin(), values.end());
    std::ranges::sort(sorted);
    return sorted;
}

bool node_id_precedes(const CirNodeId& left, const CirNodeId& right) {
    return left < right;
}

/// Canonical identity of a resolved declaration, falling back to its recorded
/// module and local name when semantic analysis stored no `SymbolId`.
template <typename Decl>
std::optional<SymbolId> declaration_symbol(const Decl& decl, SymbolKind kind, const std::string& map_key) {
    if (decl.symbol_id.has_value()) {
        return decl.symbol_id;
    }
    if (decl.module_name.empty()) {
        return std::nullopt;
    }
    return make_symbol_id(kind, decl.module_name, decl.name.empty() ? map_key : decl.name);
}

std::vector<CirTraitField> lower_trait_fields(const std::vector<ResolvedField>& fields) {
    std::vector<CirTraitField> lowered;
    lowered.reserve(fields.size());
    for (const auto& field : fields) {
        lowered.push_back(CirTraitField{.name        = field.name,
                                        .type        = field.type.name,
                                        .is_var      = field.is_var,
                                        .is_persist  = field.is_persist,
                                        .is_sync     = field.is_sync,
                                        .has_default = field.has_default});
    }
    return lowered;
}

std::vector<CirTrait> lower_traits(const DecoratedProgram& program) {
    std::vector<CirTrait> traits;
    traits.reserve(program.traits.size());
    for (const auto& [key, trait] : program.traits) {
        const auto symbol = declaration_symbol(trait, SymbolKind::Trait, key);
        if (!symbol.has_value()) {
            continue;
        }
        traits.push_back(CirTrait{.symbol = *symbol, .fields = lower_trait_fields(trait.fields)});
    }
    std::ranges::sort(
        traits, [](const CirTrait& left, const CirTrait& right) { return symbol_precedes(left.symbol, right.symbol); });
    return traits;
}

std::vector<BoundTraitAccess> sorted_bound_reads(const std::vector<BoundTraitAccess>& reads) {
    std::vector<BoundTraitAccess> sorted = reads;
    std::ranges::sort(sorted, [](const BoundTraitAccess& left, const BoundTraitAccess& right) {
        if (left.binding_index != right.binding_index) {
            return left.binding_index < right.binding_index;
        }
        return symbol_precedes(left.trait, right.trait);
    });
    return sorted;
}

std::vector<InferredHandlerCommand> sorted_commands(const std::vector<InferredHandlerCommand>& commands) {
    std::vector<InferredHandlerCommand> sorted = commands;
    std::ranges::sort(sorted, [](const InferredHandlerCommand& left, const InferredHandlerCommand& right) {
        if (left.kind != right.kind) {
            return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
        }
        const auto lhs = left.target.has_value() ? make_canonical_id(*left.target) : std::string{};
        const auto rhs = right.target.has_value() ? make_canonical_id(*right.target) : std::string{};
        return lhs < rhs;
    });
    return sorted;
}

CirHandlerNode lower_handler(const HandlerNode& handler) {
    const auto& contract = handler.contract;
    CirHandlerNode node{.id                = handler_node_id(handler.identity),
                        .rule              = handler.identity.rule,
                        .trigger           = handler.identity.trigger,
                        .implementation    = handler.implementation,
                        .domain            = contract.domain_kind,
                        .selection         = contract.selection,
                        .exclusion         = contract.exclusion,
                        .bindings          = contract.pair_bindings,
                        .spatial_join      = contract.spatial_join,
                        .reads             = sorted_symbols(contract.reads),
                        .bound_reads       = sorted_bound_reads(contract.bound_reads),
                        .writes            = sorted_symbols(contract.writes),
                        .projects          = sorted_symbols(contract.projects),
                        .emits             = sorted_symbols(contract.emits),
                        .commands          = sorted_commands(contract.commands),
                        .effects           = sorted_strings(contract.effects),
                        .explicit_after    = {},
                        .declaration_order = handler.declaration_order,
                        .location          = handler.location};
    node.explicit_after.reserve(handler.explicit_after.size());
    for (const auto& predecessor : handler.explicit_after) {
        node.explicit_after.push_back(handler_node_id(predecessor));
    }
    std::ranges::sort(node.explicit_after, node_id_precedes);
    return node;
}

/// The access modes `contract` exhibits for `trait`, in fixed enum order so the
/// list is canonical without a sort.
std::vector<CirTraitAccessMode> access_modes(const HandlerContract& contract, const SymbolId& trait) {
    std::vector<CirTraitAccessMode> modes;
    if (contract.reads.contains(trait)) {
        modes.push_back(CirTraitAccessMode::Read);
    }
    if (contract.writes.contains(trait)) {
        modes.push_back(CirTraitAccessMode::Write);
    }
    if (contract.projects.contains(trait)) {
        modes.push_back(CirTraitAccessMode::Project);
    }
    if (std::ranges::find(contract.selection, trait) != contract.selection.end()) {
        modes.push_back(CirTraitAccessMode::Select);
    }
    return modes;
}

std::optional<CirNodeKind> producer_node_kind(EventProducerKind kind) {
    switch (kind) {
        case EventProducerKind::Handler:
            return std::nullopt;
        case EventProducerKind::ExternalSource:
            return CirNodeKind::ExternalEventSource;
        case EventProducerKind::SchedulerBoundary:
            return CirNodeKind::SchedulerBoundary;
        case EventProducerKind::ActivationCommit:
            return CirNodeKind::ActivationCommit;
    }
    std::unreachable();
}

void lower_rule_groups(CirProgram& cir) {
    std::vector<CirRuleGroup> groups;
    for (const auto& handler : cir.handlers) {
        auto found =
            std::ranges::find_if(groups, [&](const CirRuleGroup& group) { return group.rule == handler.rule; });
        if (found == groups.end()) {
            groups.push_back(CirRuleGroup{.rule = handler.rule, .handlers = {}});
            found = std::prev(groups.end());
        }
        found->handlers.push_back(handler.id);
    }
    std::ranges::sort(groups, [](const CirRuleGroup& left, const CirRuleGroup& right) {
        return symbol_precedes(left.rule, right.rule);
    });
    for (auto& group : groups) {
        std::ranges::sort(group.handlers, node_id_precedes);
    }
    cir.rule_groups = std::move(groups);
}

/// Owns the per-program lookup tables lowering needs, so each pass resolves
/// identities once instead of rescanning the graph.
class Lowerer {
public:
    explicit Lowerer(const DecoratedProgram& program)
        : program_(&program) {
        for (const auto& handler : program.execution_graph.handlers) {
            handlers_by_identity_.emplace(handler.identity, &handler);
        }
        for (const auto& phase : program.execution_graph.phases) {
            planned_phases_.insert(make_canonical_id(phase.phase));
        }
    }

    CirProgram lower() {
        CirProgram cir;
        cir.modules = program_->linked_modules;
        cir.traits  = lower_traits(*program_);
        lower_nodes(cir);
        lower_relations(cir);
        lower_activation_schedules(cir);
        return cir;
    }

private:
    void lower_nodes(CirProgram& cir) {
        const auto& graph = program_->execution_graph;

        cir.handlers.reserve(graph.handlers.size());
        for (const auto& handler : graph.handlers) {
            cir.handlers.push_back(lower_handler(handler));
        }
        std::ranges::sort(cir.handlers, [](const CirHandlerNode& left, const CirHandlerNode& right) {
            return node_id_precedes(left.id, right.id);
        });

        lower_rule_groups(cir);
        lower_phase_nodes(cir);
        lower_event_producer_nodes(cir);

        cir.rasterizations.reserve(graph.render_passes.size());
        for (const auto& pass : graph.render_passes) {
            cir.rasterizations.push_back(
                CirRasterizationNode{.id = rasterization_node_id(pass.phase), .phase = pass.phase});
        }
        std::ranges::sort(cir.rasterizations, [](const CirRasterizationNode& left, const CirRasterizationNode& right) {
            return node_id_precedes(left.id, right.id);
        });
    }

    void lower_phase_nodes(CirProgram& cir) {
        const auto& graph = program_->execution_graph;
        for (const auto& phase : graph.phases) {
            cir.phases.push_back(CirPhaseNode{.id                  = phase_node_id(phase.phase),
                                              .phase               = phase.phase,
                                              .declared            = true,
                                              .source_dependencies = phase.source_dependencies,
                                              .runtime_root        = phase.runtime_root,
                                              .every_seconds       = phase.every_seconds,
                                              .max_repetitions     = phase.max_repetitions,
                                              .declaration_order   = phase.declaration_order});
        }
        // A phase named by a dependency, barrier, or render pass whose own plan
        // belongs to another compilation unit still has to be addressable, or
        // those relations would lose an endpoint.
        const auto reference_phase = [&](const SymbolId& phase) {
            const auto canonical = make_canonical_id(phase);
            if (planned_phases_.contains(canonical) || !referenced_phases_.insert(canonical).second) {
                return;
            }
            cir.phases.push_back(CirPhaseNode{.id = phase_node_id(phase), .phase = phase, .declared = false});
        };
        for (const auto& phase : graph.phases) {
            for (const auto& upstream : phase.completion_dependencies) {
                reference_phase(upstream);
            }
        }
        for (const auto& barrier : graph.phase_barriers) {
            reference_phase(barrier.upstream_phase);
        }
        for (const auto& pass : graph.render_passes) {
            reference_phase(pass.phase);
        }
        std::ranges::sort(cir.phases, [](const CirPhaseNode& left, const CirPhaseNode& right) {
            return node_id_precedes(left.id, right.id);
        });
    }

    void lower_event_producer_nodes(CirProgram& cir) {
        std::unordered_set<std::string> seen;
        for (const auto& flow : program_->execution_graph.event_flows) {
            const auto kind = producer_node_kind(flow.producer.kind);
            if (!kind.has_value()) {
                continue;
            }
            auto id = event_producer_node_id(*kind, flow.event);
            if (!seen.insert(id.value).second) {
                continue;
            }
            cir.event_producers.push_back(
                CirEventProducerNode{.id = std::move(id), .kind = *kind, .event = flow.event});
        }
        std::ranges::sort(cir.event_producers, [](const CirEventProducerNode& left, const CirEventProducerNode& right) {
            return node_id_precedes(left.id, right.id);
        });
    }

    void lower_schedule_relations(CirProgram& cir) {
        for (const auto& edge : program_->execution_graph.schedule_edges) {
            const auto before = handlers_by_identity_.find(edge.before);
            const auto after  = handlers_by_identity_.find(edge.after);
            // An explicit `after:` naming a handler outside this program stays
            // on the dependent node rather than becoming a dangling relation —
            // the same filter the scheduler applies when building its adjacency.
            if (before == handlers_by_identity_.end() || after == handlers_by_identity_.end()) {
                continue;
            }
            CirScheduleRelation relation{.before            = handler_node_id(edge.before),
                                         .after             = handler_node_id(edge.after),
                                         .kind              = edge.kind,
                                         .orientation       = edge.orientation,
                                         .trait_provenance  = {},
                                         .effect_provenance = edge.effect_provenance};
            relation.trait_provenance.reserve(edge.trait_provenance.size());
            for (const auto& trait : edge.trait_provenance) {
                relation.trait_provenance.push_back(
                    CirTraitProvenance{.trait  = trait,
                                       .before = access_modes(before->second->contract, trait),
                                       .after  = access_modes(after->second->contract, trait)});
            }
            std::ranges::sort(relation.effect_provenance);
            cir.schedule_dependencies.push_back(std::move(relation));
        }
        std::ranges::sort(cir.schedule_dependencies, schedule_relation_precedes);
    }

    void lower_relations(CirProgram& cir) {
        const auto& graph = program_->execution_graph;
        lower_schedule_relations(cir);

        for (const auto& phase : graph.phases) {
            for (const auto& upstream : phase.completion_dependencies) {
                cir.phase_dependencies.push_back(CirPhaseDependencyRelation{.upstream   = phase_node_id(upstream),
                                                                            .downstream = phase_node_id(phase.phase)});
            }
        }
        std::ranges::sort(cir.phase_dependencies, phase_dependency_precedes);

        for (const auto& barrier : graph.phase_barriers) {
            cir.phase_barriers.push_back(
                CirPhaseBarrierRelation{.upstream_phase     = phase_node_id(barrier.upstream_phase),
                                        .downstream_handler = handler_node_id(barrier.downstream_handler)});
        }
        std::ranges::sort(cir.phase_barriers, phase_barrier_precedes);

        for (const auto& flow : graph.event_flows) {
            const auto kind = producer_node_kind(flow.producer.kind);
            cir.event_flows.push_back(CirEventFlowRelation{.producer = kind.has_value()
                                                                           ? event_producer_node_id(*kind, flow.event)
                                                                           : handler_node_id(*flow.producer.handler),
                                                           .event    = flow.event,
                                                           .consumer = handler_node_id(flow.consumer)});
        }
        std::ranges::sort(cir.event_flows, event_flow_precedes);

        for (const auto& pass : graph.render_passes) {
            const auto raster = rasterization_node_id(pass.phase);
            cir.render_pass_flows.push_back(
                CirRenderFlowRelation{.before = handler_node_id(pass.vertex_handler), .after = raster});
            cir.render_pass_flows.push_back(
                CirRenderFlowRelation{.before = raster, .after = handler_node_id(pass.fragment_handler)});
            cir.render_pass_flows.push_back(CirRenderFlowRelation{.before = handler_node_id(pass.fragment_handler),
                                                                  .after  = phase_node_id(pass.phase)});
        }
        std::ranges::sort(cir.render_pass_flows, render_flow_precedes);
    }

    void lower_activation_schedules(CirProgram& cir) {
        const auto& graph = program_->execution_graph;
        std::vector<ResolvedHandlerTrigger> activations;
        for (const auto& level : graph.dependency_levels) {
            if (std::ranges::find(activations, level.activation) == activations.end()) {
                activations.push_back(level.activation);
            }
        }
        for (const auto& activation : activations) {
            CirActivationSchedule schedule{.activation = activation, .stable_order = {}, .levels = {}};
            for (const auto& identity : graph.stable_topological_order) {
                if (identity.trigger == activation) {
                    schedule.stable_order.push_back(handler_node_id(identity));
                }
            }
            for (const auto& level : graph.dependency_levels) {
                if (level.activation != activation) {
                    continue;
                }
                CirDependencyLevel lowered{.index = level.index, .handlers = {}};
                lowered.handlers.reserve(level.handlers.size());
                for (const auto& identity : level.handlers) {
                    lowered.handlers.push_back(handler_node_id(identity));
                }
                schedule.levels.push_back(std::move(lowered));
            }
            cir.activation_schedules.push_back(std::move(schedule));
        }
        std::ranges::sort(cir.activation_schedules,
                          [](const CirActivationSchedule& left, const CirActivationSchedule& right) {
                              return trigger_precedes(left.activation, right.activation);
                          });
    }

    const DecoratedProgram* program_;
    std::unordered_map<HandlerIdentity, const HandlerNode*, HandlerIdentityHash> handlers_by_identity_;
    std::unordered_set<std::string> planned_phases_;
    std::unordered_set<std::string> referenced_phases_;
};

}  // namespace

CirProgram lower_program(const DecoratedProgram& program) {
    return Lowerer(program).lower();
}

}  // namespace cactus::cir
