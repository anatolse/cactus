#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

namespace cactus {

// Rejects every reachable `add S.V` of a state with arrows that a possible source has no arrow for.
// Outside the stdlib, also rejects an arrow no reachable `add` takes and a variant nothing enters.
// Runs on the whole linked program, since an `add` can be in another module than its state.
void validate_state_transitions(const DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
