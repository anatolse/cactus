#include "cir/cir_graph_projection.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cactus::cir {
namespace {

std::string joined_modes(const std::vector<CirTraitAccessMode>& modes) {
    if (modes.empty()) {
        return "none";
    }
    std::string joined;
    for (const auto mode : modes) {
        if (!joined.empty()) {
            joined += '+';
        }
        joined += cir_trait_access_mode_name(mode);
    }
    return joined;
}

std::string trait_label(const std::vector<CirTraitProvenance>& provenance) {
    std::string label;
    for (const auto& entry : provenance) {
        if (!label.empty()) {
            label += ", ";
        }
        label += make_canonical_id(entry.trait);
        label += " (";
        label += joined_modes(entry.before);
        label += '/';
        label += joined_modes(entry.after);
        label += ')';
    }
    return label;
}

std::string effect_label(const std::vector<std::string>& effects) {
    std::string label = "effect ";
    for (std::size_t index = 0; index < effects.size(); ++index) {
        if (index > 0) {
            label += ", ";
        }
        label += effects[index];
    }
    return label;
}

CirEdgeStyle schedule_style(const CirScheduleRelation& relation) {
    switch (relation.kind) {
        case ScheduleEdgeKind::ExplicitHandler:
        case ScheduleEdgeKind::ExplicitRule:
            return CirEdgeStyle::ScheduleExplicit;
        case ScheduleEdgeKind::EffectConflict:
            return CirEdgeStyle::ScheduleEffect;
        case ScheduleEdgeKind::DataConflict:
            return CirEdgeStyle::ScheduleTrait;
    }
    std::unreachable();
}

std::string schedule_label(const CirScheduleRelation& relation) {
    switch (schedule_style(relation)) {
        case CirEdgeStyle::ScheduleExplicit:
            return "after";
        case CirEdgeStyle::ScheduleEffect:
            return effect_label(relation.effect_provenance);
        default:
            return trait_label(relation.trait_provenance);
    }
}

std::string producer_label(CirNodeKind kind, const SymbolId& event) {
    return std::string{cir_node_kind_name(kind)} + ": " + make_canonical_id(event);
}

/// Collects nodes, assigns short display IDs after canonical sorting, and
/// resolves relation endpoints through one index.
class Projector {
public:
    explicit Projector(const CirProgram& cir)
        : cir_(&cir) {}

    CirGraphProjection build() {
        collect_nodes();
        collect_groups();
        collect_edges();
        return std::move(projection_);
    }

private:
    void add_node(const CirNodeId& id, CirNodeKind kind, std::string label) {
        projection_.nodes.push_back(CirProjectedNode{.display_id = {},
                                                     .id         = id,
                                                     .kind       = kind,
                                                     .label      = std::move(label),
                                                     .group      = CirGraphProjection::NO_GROUP});
    }

    void collect_nodes() {
        for (const auto& handler : cir_->handlers) {
            add_node(handler.id, CirNodeKind::Handler, handler.id.value);
        }
        for (const auto& phase : cir_->phases) {
            add_node(phase.id,
                     CirNodeKind::Phase,
                     "phase " + make_canonical_id(phase.phase) + (phase.declared ? "" : " (imported)"));
        }
        for (const auto& producer : cir_->event_producers) {
            add_node(producer.id, producer.kind, producer_label(producer.kind, producer.event));
        }
        for (const auto& raster : cir_->rasterizations) {
            add_node(raster.id, CirNodeKind::Rasterization, "rasterization: " + make_canonical_id(raster.phase));
        }

        std::ranges::sort(projection_.nodes, [](const CirProjectedNode& left, const CirProjectedNode& right) {
            return left.id < right.id;
        });
        for (std::size_t index = 0; index < projection_.nodes.size(); ++index) {
            projection_.nodes[index].display_id = "n" + std::to_string(index);
            index_by_id_.emplace(projection_.nodes[index].id.value, index);
        }
    }

    void collect_groups() {
        for (const auto& group : cir_->rule_groups) {
            CirProjectedGroup projected{.display_id = "g" + std::to_string(projection_.groups.size()),
                                        .label      = make_canonical_id(group.rule),
                                        .nodes      = {}};
            const auto group_index = projection_.groups.size();
            for (const auto& handler : group.handlers) {
                const auto found = index_by_id_.find(handler.value);
                if (found == index_by_id_.end()) {
                    continue;
                }
                projected.nodes.push_back(found->second);
                projection_.nodes[found->second].group = group_index;
            }
            projection_.groups.push_back(std::move(projected));
        }
    }

    void add_edge(const CirNodeId& before, const CirNodeId& after, CirEdgeStyle style, std::string label) {
        const auto from = index_by_id_.find(before.value);
        const auto to   = index_by_id_.find(after.value);
        if (from == index_by_id_.end() || to == index_by_id_.end()) {
            return;
        }
        projection_.edges.push_back(
            CirProjectedEdge{.before = from->second, .after = to->second, .style = style, .label = std::move(label)});
    }

    void collect_edges() {
        for (const auto& relation : cir_->schedule_dependencies) {
            add_edge(relation.before, relation.after, schedule_style(relation), schedule_label(relation));
        }
        for (const auto& relation : cir_->phase_dependencies) {
            add_edge(relation.upstream, relation.downstream, CirEdgeStyle::PhaseDependency, "phase");
        }
        for (const auto& relation : cir_->phase_barriers) {
            add_edge(relation.upstream_phase, relation.downstream_handler, CirEdgeStyle::PhaseBarrier, "barrier");
        }
        for (const auto& relation : cir_->event_flows) {
            add_edge(relation.producer,
                     relation.consumer,
                     CirEdgeStyle::EventFlow,
                     "event " + make_canonical_id(relation.event));
        }
        for (const auto& relation : cir_->render_pass_flows) {
            add_edge(relation.before, relation.after, CirEdgeStyle::RenderPassFlow, "render");
        }
        std::ranges::sort(projection_.edges, [](const CirProjectedEdge& left, const CirProjectedEdge& right) {
            if (left.before != right.before) {
                return left.before < right.before;
            }
            if (left.after != right.after) {
                return left.after < right.after;
            }
            if (left.style != right.style) {
                return static_cast<std::uint8_t>(left.style) < static_cast<std::uint8_t>(right.style);
            }
            return left.label < right.label;
        });
    }

    const CirProgram* cir_;
    CirGraphProjection projection_;
    std::unordered_map<std::string, std::size_t> index_by_id_;
};

}  // namespace

const char* cir_edge_style_name(CirEdgeStyle style) {
    switch (style) {
        case CirEdgeStyle::ScheduleTrait:
            return "schedule-trait";
        case CirEdgeStyle::ScheduleEffect:
            return "schedule-effect";
        case CirEdgeStyle::ScheduleExplicit:
            return "schedule-explicit";
        case CirEdgeStyle::PhaseDependency:
            return "phase-dependency";
        case CirEdgeStyle::PhaseBarrier:
            return "phase-barrier";
        case CirEdgeStyle::EventFlow:
            return "event-flow";
        case CirEdgeStyle::RenderPassFlow:
            return "render-pass-flow";
    }
    std::unreachable();
}

CirGraphProjection project_graph(const CirProgram& cir) {
    return Projector(cir).build();
}

}  // namespace cactus::cir
