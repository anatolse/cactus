#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cactus {

[[nodiscard]] std::vector<SymbolId> collider_shape_traits();

// C++ for the median collider extent over `origins`, or nullopt when any extent is not a program constant.
[[nodiscard]] std::optional<std::string> constant_collider_cell_size(const DecoratedProgram& program,
                                                                     const std::vector<std::size_t>& origins);

// Indexes into `program.reachability.origins` of the archetypes that can carry
// every trait of `required`, ascending.
[[nodiscard]] std::vector<std::size_t> member_origins(const DecoratedProgram& program,
                                                      const std::vector<SymbolId>& required);

}  // namespace cactus
