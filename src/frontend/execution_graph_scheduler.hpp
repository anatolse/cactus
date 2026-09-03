#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <unordered_set>

namespace cactus {

/// Canonical identities of events the embedding host injects (`pub extern`).
/// Passed in because external provenance lives on `ResolvedEvent`, not on the
/// graph; it is only ever looked up, never iterated, so its unordered order
/// cannot leak into graph output.
using ExternalEventSet = std::unordered_set<SymbolId, SymbolIdHash>;

/// Collects the canonical identities of host-injected events from a program's
/// resolved events, so both the single-module and linked paths derive the
/// scheduler's external-event input the same way.
ExternalEventSet collect_external_events(const std::unordered_map<std::string, ResolvedEvent>& events);

// Shared handler-scheduling core used by both single-module semantic analysis
// (SemanticAnalyzer::build_dependency_graph + validate_after_clauses) and
// cross-module program linking (ProgramLinker::rebuild_execution_graph).
//
// Operates purely on graph.handlers, graph.phases, and whatever explicit
// schedule_edges the caller has already inserted (from AST `after:` clauses
// or from cross-module linking) — it does not know or care which. Computes:
//   - phase-barrier and event-flow edges (from graph.phases/graph.handlers)
//   - runtime-owned event producers (scheduler boundary, activation commit,
//     host external source) for consumed triggers
//   - data/effect conflict edges between co-triggered handlers
//   - handler dependency cycles (each distinct cycle reported once)
//   - per-activation topological dependency levels
//
// Returns false if any error was reported to `errors` during scheduling.
bool compute_handler_schedule(ExecutionGraph& graph, const ExternalEventSet& external_events, ErrorReporter& errors);

}  // namespace cactus
