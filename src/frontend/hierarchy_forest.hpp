#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cactus {

// The hierarchy is built from `std.core.Parent`, or from a program's own trait named `Parent`.
[[nodiscard]] bool is_hierarchy_trait(const SymbolId& trait);

// `std.core.Parent` when the program has it, else a local trait named `Parent`.
[[nodiscard]] std::optional<SymbolId> find_hierarchy_trait(const DecoratedProgram& program);

// The entity's canonical id, else its name.
[[nodiscard]] std::string declared_entity_identity(const EntityNode& entity);

struct DeclaredEntityOrder {
    std::vector<const EntityNode*> order;   // each entity after the declared entity its Parent names
    std::vector<const EntityNode*> cyclic;  // entities on a Parent cycle, sorted by canonical identity
};

// A stable topological order: entities that don't name each other keep their given order.
[[nodiscard]] DeclaredEntityOrder parent_first_order(const std::vector<const EntityNode*>& entities);

// Classifies every reachable source of a `Parent` value and stores the result
// in `program.hierarchy`. Uses `program.reachability` when it was derived.
void derive_hierarchy_facts(DecoratedProgram& program);

}  // namespace cactus
