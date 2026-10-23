#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <optional>
#include <string>

namespace cactus {

// The trait a creation site names, when that trait declares a ranged field.
[[nodiscard]] const ResolvedTrait* find_ranged_trait(const DecoratedProgram& program,
                                                     const std::optional<SymbolId>& symbol,
                                                     const std::string& source_name);

// One check function per trait with ranged fields, each returning the first failure of a value.
// Empty when no trait declares a range, so such programs carry no checks.
[[nodiscard]] std::string emit_range_check_functions(const DecoratedProgram& program);

// A bool expression: whether `value` of `trait` passes its check, reporting the fault when not.
[[nodiscard]] std::string emit_range_check_passes(const ResolvedTrait& trait, const std::string& value);

// Reports and runs `on_fault` when `value` of `trait` fails its check.
[[nodiscard]] std::string emit_range_check_statement(const ResolvedTrait& trait,
                                                     const std::string& value,
                                                     const std::string& on_fault,
                                                     const std::string& indent);

}  // namespace cactus
