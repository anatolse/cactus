#pragma once

#include "common/persistence_metadata.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cactus {

// Archetype -> every trait its flattened tree carries, children included.
using TemplateTraitLookup = std::unordered_map<SymbolId, std::vector<SymbolId>, SymbolIdHash>;

[[nodiscard]] TemplateTraitLookup template_trait_lookup(const ModulePersistenceMetadata& persistence);

// Fills graph.lifecycle_flows, marks every event and lifecycle edge inside a
// delivery cycle as delayed, and records graph.activation_depths. Run after
// compute_handler_schedule. A spawn whose template is not in `templates`
// counts as adding every watched trait.
void classify_delivery_cycles(ExecutionGraph& graph, const TemplateTraitLookup& templates);

// The delivery cycle of each handler on one, numbered densely from 0 in
// canonical-id order. Handlers on no cycle are absent.
[[nodiscard]] std::unordered_map<HandlerIdentity, std::uint32_t, HandlerIdentityHash> delivery_components(
    const ExecutionGraph& graph);

// One info per delivery cycle with a handler declared outside the stdlib, then a
// warning when one run of a handler can feed its cycle more than once. Run on the
// whole program: a module graph holds only part of a cycle.
void report_delayed_cycles(const DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
