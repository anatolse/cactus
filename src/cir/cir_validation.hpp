#pragma once

#include "common/error_reporter.hpp"

#include "cir/cir.hpp"

namespace cactus::cir {

/// Checks CIR-internal invariants: unique node IDs, resolvable relation
/// endpoints, canonical array ordering, acyclic schedule relations, and
/// dependency levels consistent with their activation's stable order.
///
/// Reports every violation it finds rather than stopping at the first, and
/// never adds, orients, or otherwise repairs an execution dependency — a
/// diagnosis here means lowering or the graph upstream of it is wrong.
/// Event-flow relations are deliberately exempt from cycle checking.
bool validate_program(const CirProgram& cir, ErrorReporter& errors);

}  // namespace cactus::cir
