#pragma once

#include "common/error_reporter.hpp"
#include "frontend/delivery_cycles.hpp"
#include "frontend/semantic_analyzer.hpp"

namespace cactus {

// Shared handler-scheduling core used by both single-module semantic analysis
// (SemanticAnalyzer::build_dependency_graph + validate_after_clauses) and
// cross-module program linking (ProgramLinker::rebuild_execution_graph).
//
// Operates purely on graph.handlers, graph.phases, and whatever explicit
// schedule_edges the caller has already inserted (from AST `after:` clauses
// or from cross-module linking) — it does not know or care which. Computes:
//   - phase-barrier and event-flow edges (from graph.phases/graph.handlers)
//   - lifecycle flows, delayed edges and activation depths (classify_delivery_cycles)
//   - data/effect/channel conflict edges between co-triggered handlers,
//     including occurrence races between the runs they reach
//   - handler dependency cycles (each distinct cycle reported once)
//   - per-activation topological dependency levels
//
// Returns false if any error was reported to `errors` during scheduling.
bool compute_handler_schedule(ExecutionGraph& graph, const TemplateTraitLookup& templates, ErrorReporter& errors);

// Adds one ExplicitGroup edge per member handler for each of graph.group_orderings.
// Run before compute_handler_schedule, on a module graph or on the linked program.
void expand_group_orderings(ExecutionGraph& graph);

// Rejects `on added T` / `on removed T` when some handler in the graph projects T.
void validate_lifecycle_trigger_traits(const ExecutionGraph& graph, ErrorReporter& errors);

}  // namespace cactus
