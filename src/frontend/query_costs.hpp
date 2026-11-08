#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

namespace cactus {

// Warns about superlinear world scans and full scans for one key, using
// `program.reachability` to tell what only named entities or a `single` trait
// bound. Without derived facts, every domain and filter counts as unbounded.
void check_query_costs(const DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
