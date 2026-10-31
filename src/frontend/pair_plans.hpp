#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

namespace cactus {

// Bounds product passes whose binding only named entities can fill, using
// `program.reachability` when it was derived, then warns once per pair rule
// still planned as a product.
void finalize_pair_plans(DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
