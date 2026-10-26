#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <string>
#include <vector>

namespace cactus {

// The program's relations, sorted by canonical id so generated code is stable.
[[nodiscard]] std::vector<const ResolvedRelation*> sorted_relations(const DecoratedProgram& program);

// Row structs and setup functions; empty for a program without relations.
[[nodiscard]] std::string emit_relation_declarations(const DecoratedProgram& program);

// One row struct per relation, in its module's namespace.
[[nodiscard]] std::string emit_relation_rows(const DecoratedProgram& program);

// `generated_connect_relations(registry)`, which ties rows to their ends'
// lifetime, and `generated_compact_relations(registry)`, run at each commit.
[[nodiscard]] std::string emit_relation_setup(const DecoratedProgram& program);

}  // namespace cactus
