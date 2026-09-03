#pragma once

#include "cir/cir.hpp"

#include <string>

namespace cactus::cir {

/// Renders CIR as a deterministic DOT digraph: rule clusters, short `n<N>`
/// node IDs assigned after canonical sorting, and a distinct arrow style per
/// relation kind. Explicitly lossy — a projection for reading, not interchange.
[[nodiscard]] std::string write_dot(const CirProgram& cir);

/// Renders CIR as a deterministic Mermaid flowchart using the same projection
/// and the same short node IDs as `write_dot`.
[[nodiscard]] std::string write_mermaid(const CirProgram& cir);

}  // namespace cactus::cir
