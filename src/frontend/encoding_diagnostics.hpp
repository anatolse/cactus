#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

namespace cactus {

// Warns about plain trait fields that encode a sum or a link by hand: a `bool` flag that guards
// other fields, a `bool` used only as a filter, and an `entity_id` used only in joins.
void check_encodings(const DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
