#pragma once

#include "cir/cir.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cactus::cir {

/// How a projected edge should read to someone looking at the diagram. Kept
/// separate from `ScheduleEdgeKind` because the projection also carries phase,
/// event, and render-pass relations that are not schedule edges at all.
enum class CirEdgeStyle : std::uint8_t {
    ScheduleTrait,     // trait-caused ordering within one activation
    ScheduleEffect,    // serialized shared effect
    ScheduleExplicit,  // author-declared `after:`
    PhaseDependency,   // phase completion feeding the next phase
    PhaseBarrier,      // phase completion gating a handler
    EventFlow,         // event delivery; may legally be part of a cycle
    RenderPassFlow,    // vertex -> rasterization -> fragment -> phase
};

[[nodiscard]] const char* cir_edge_style_name(CirEdgeStyle style);

/// One drawable node. `display_id` is a short, format-safe identifier assigned
/// after canonical sorting; `id` keeps the stable CIR identity for the label.
struct CirProjectedNode {
    std::string display_id;
    CirNodeId id;
    CirNodeKind kind = CirNodeKind::Handler;
    std::string label;
    /// Index into `CirGraphProjection::groups`, or `NO_GROUP` for standalone
    /// synthetic and phase nodes.
    std::size_t group = 0;
};

/// A rule's handlers, drawn as one cluster. Grouping is presentation only — the
/// handlers inside stay separately addressable nodes.
struct CirProjectedGroup {
    std::string display_id;
    std::string label;
    std::vector<std::size_t> nodes;
};

struct CirProjectedEdge {
    std::size_t before = 0;
    std::size_t after  = 0;
    CirEdgeStyle style = CirEdgeStyle::ScheduleTrait;
    std::string label;
};

struct CirGraphProjection {
    static constexpr std::size_t NO_GROUP = static_cast<std::size_t>(-1);

    std::vector<CirProjectedNode> nodes;
    std::vector<CirProjectedGroup> groups;
    std::vector<CirProjectedEdge> edges;
};

/// Builds the single deterministic projection both graphical writers consume,
/// so node selection, grouping, edge semantics, and ordering are decided once.
/// The result is explicitly lossy: it carries labels, not full contracts.
[[nodiscard]] CirGraphProjection project_graph(const CirProgram& cir);

}  // namespace cactus::cir
