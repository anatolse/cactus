#include "cir/cir_graphviz.hpp"

#include "cir/cir_graph_projection.hpp"
#include "cir/cir_text.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace cactus::cir {
namespace {

constexpr std::array<CirEdgeStyle, 7> EDGE_STYLES{
    CirEdgeStyle::ScheduleTrait,
    CirEdgeStyle::ScheduleEffect,
    CirEdgeStyle::ScheduleExplicit,
    CirEdgeStyle::PhaseDependency,
    CirEdgeStyle::PhaseBarrier,
    CirEdgeStyle::EventFlow,
    CirEdgeStyle::RenderPassFlow,
};

const char* dot_node_shape(CirNodeKind kind) {
    switch (kind) {
        case CirNodeKind::Handler:
            return "box";
        case CirNodeKind::Phase:
            return "ellipse";
        case CirNodeKind::ExternalEventSource:
            return "hexagon";
        case CirNodeKind::SchedulerBoundary:
            return "house";
        case CirNodeKind::ActivationCommit:
            return "invhouse";
        case CirNodeKind::Rasterization:
            return "parallelogram";
    }
    std::unreachable();
}

/// Arrow attributes per relation kind. Event flow drops the layout constraint
/// so a legal producer/consumer cycle stays readable and visibly separate from
/// the acyclic schedule edges around it.
const char* dot_edge_attributes(CirEdgeStyle style) {
    switch (style) {
        case CirEdgeStyle::ScheduleTrait:
            return R"(color="#333333", style=solid)";
        case CirEdgeStyle::ScheduleEffect:
            return R"(color="#ef6c00", style=dashed)";
        case CirEdgeStyle::ScheduleExplicit:
            return R"(color="#6a1b9a", style=bold)";
        case CirEdgeStyle::PhaseDependency:
            return R"(color="#1565c0", style=solid)";
        case CirEdgeStyle::PhaseBarrier:
            return R"(color="#1565c0", style=dotted)";
        case CirEdgeStyle::EventFlow:
            return R"(color="#2e7d32", style=dashed, constraint=false)";
        case CirEdgeStyle::RenderPassFlow:
            return R"(color="#ad1457", style=solid)";
    }
    std::unreachable();
}

std::pair<const char*, const char*> mermaid_node_brackets(CirNodeKind kind) {
    switch (kind) {
        case CirNodeKind::Handler:
            return {"[\"", "\"]"};
        case CirNodeKind::Phase:
            return {"([\"", "\"])"};
        case CirNodeKind::ExternalEventSource:
        case CirNodeKind::SchedulerBoundary:
        case CirNodeKind::ActivationCommit:
            return {"{{\"", "\"}}"};
        case CirNodeKind::Rasterization:
            return {"[/\"", "\"/]"};
    }
    std::unreachable();
}

const char* mermaid_arrow(CirEdgeStyle style) {
    switch (style) {
        case CirEdgeStyle::ScheduleTrait:
        case CirEdgeStyle::PhaseDependency:
        case CirEdgeStyle::RenderPassFlow:
            return "-->";
        case CirEdgeStyle::ScheduleExplicit:
            return "==>";
        case CirEdgeStyle::ScheduleEffect:
        case CirEdgeStyle::PhaseBarrier:
        case CirEdgeStyle::EventFlow:
            return "-.->";
    }
    std::unreachable();
}

const char* mermaid_stroke(CirEdgeStyle style) {
    switch (style) {
        case CirEdgeStyle::ScheduleTrait:
            return "#333333";
        case CirEdgeStyle::ScheduleEffect:
            return "#ef6c00";
        case CirEdgeStyle::ScheduleExplicit:
            return "#6a1b9a";
        case CirEdgeStyle::PhaseDependency:
        case CirEdgeStyle::PhaseBarrier:
            return "#1565c0";
        case CirEdgeStyle::EventFlow:
            return "#2e7d32";
        case CirEdgeStyle::RenderPassFlow:
            return "#ad1457";
    }
    std::unreachable();
}

void append_dot_node(std::string& out, const CirProjectedNode& node, const char* indent) {
    out += indent;
    out += node.display_id;
    out += " [label=\"";
    append_dot_escaped(out, node.label);
    out += "\", shape=";
    out += dot_node_shape(node.kind);
    out += "];\n";
}

void append_mermaid_node(std::string& out, const CirProjectedNode& node, const char* indent) {
    const auto [open, close] = mermaid_node_brackets(node.kind);
    out += indent;
    out += node.display_id;
    out += open;
    append_mermaid_escaped(out, node.label);
    out += close;
    out += '\n';
}

/// Emits the `linkStyle` lines that colour each relation kind, grouped so one
/// line covers every edge of a kind.
void append_mermaid_link_styles(std::string& out, const std::vector<CirProjectedEdge>& edges) {
    for (const auto style : EDGE_STYLES) {
        std::string indices;
        for (std::size_t index = 0; index < edges.size(); ++index) {
            if (edges[index].style != style) {
                continue;
            }
            if (!indices.empty()) {
                indices += ',';
            }
            indices += std::to_string(index);
        }
        if (indices.empty()) {
            continue;
        }
        out += "  linkStyle " + indices + " stroke:" + mermaid_stroke(style) + ";\n";
    }
}

}  // namespace

std::string write_dot(const CirProgram& cir) {
    const auto projection = project_graph(cir);

    std::string out = "digraph cactus_cir {\n";
    out += "  rankdir=LR;\n";
    out += "  node [fontname=\"monospace\"];\n";
    out += "  edge [fontname=\"monospace\"];\n";

    for (const auto& group : projection.groups) {
        out += "  subgraph cluster_" + group.display_id + " {\n";
        out += "    label=\"";
        append_dot_escaped(out, group.label);
        out += "\";\n";
        out += "    style=rounded;\n";
        for (const auto node_index : group.nodes) {
            append_dot_node(out, projection.nodes[node_index], "    ");
        }
        out += "  }\n";
    }
    for (const auto& node : projection.nodes) {
        if (node.group == CirGraphProjection::NO_GROUP) {
            append_dot_node(out, node, "  ");
        }
    }
    for (const auto& edge : projection.edges) {
        out += "  " + projection.nodes[edge.before].display_id + " -> " + projection.nodes[edge.after].display_id +
               " [label=\"";
        append_dot_escaped(out, edge.label);
        out += "\", ";
        out += dot_edge_attributes(edge.style);
        out += "];\n";
    }
    out += "}\n";
    return out;
}

std::string write_mermaid(const CirProgram& cir) {
    const auto projection = project_graph(cir);

    std::string out = "flowchart LR\n";
    for (const auto& group : projection.groups) {
        out += "  subgraph " + group.display_id + "[\"";
        append_mermaid_escaped(out, group.label);
        out += "\"]\n";
        for (const auto node_index : group.nodes) {
            append_mermaid_node(out, projection.nodes[node_index], "    ");
        }
        out += "  end\n";
    }
    for (const auto& node : projection.nodes) {
        if (node.group == CirGraphProjection::NO_GROUP) {
            append_mermaid_node(out, node, "  ");
        }
    }
    for (const auto& edge : projection.edges) {
        out += "  " + projection.nodes[edge.before].display_id + " " + mermaid_arrow(edge.style) + "|\"";
        append_mermaid_escaped(out, edge.label);
        out += "\"| " + projection.nodes[edge.after].display_id + "\n";
    }
    append_mermaid_link_styles(out, projection.edges);
    return out;
}

}  // namespace cactus::cir
