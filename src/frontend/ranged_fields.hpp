#pragma once

#include "common/error_reporter.hpp"
#include "frontend/semantic_analyzer.hpp"

namespace cactus {

// Rejects every `add T` and `project T` of a trait that some ranged field reads as its container.
// Runs on the whole linked program, since a container can be declared in another module.
void validate_container_traits(const DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
