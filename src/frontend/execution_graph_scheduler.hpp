#pragma once

#include "common/error_reporter.hpp"
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
//   - data/effect conflict edges between co-triggered handlers
//   - handler dependency cycles (each distinct cycle reported once)
//   - per-activation topological dependency levels
//
// Returns false if any error was reported to `errors` during scheduling.
bool compute_handler_schedule(ExecutionGraph& graph, ErrorReporter& errors);

}  // namespace cactus
