#pragma once

#include "common/error_reporter.hpp"
#include "frontend/execution_graph_scheduler.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <vector>

namespace cactus {

// The program's `single` traits, sorted by canonical id.
[[nodiscard]] std::vector<SymbolId> single_trait_ids(const DecoratedProgram& program);

[[nodiscard]] SingleTraitFacts single_trait_facts(const DecoratedProgram& program);

// Rejects a template or entity tree that carries one `single` trait on more
// than one node, and two named entities that carry it. Runs on the whole
// linked program, since the entities can be in different modules.
void validate_single_traits(const DecoratedProgram& program, ErrorReporter& errors);

}  // namespace cactus
