#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <vector>

namespace cactus {

// Which entities the program may change some traits of after construction.
// An entity that carries every trait of one of `target_sets`, or is one of
// `named_entities`, may be written; any other entity keeps the values it was
// constructed with. Adding or removing a trait is not a write: it constructs
// or destroys the trait.
struct TraitWriters {
    std::vector<std::vector<SymbolId>> target_sets;  // sorted, unique, each sorted
    std::vector<SymbolId> named_entities;            // sorted, unique
};

[[nodiscard]] TraitWriters trait_writers(const DecoratedProgram& program, const std::vector<SymbolId>& traits);

}  // namespace cactus
