#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <expected>
#include <string>
#include <string_view>

namespace cactus {

// For every phase and host-event root of a linked program, the handlers it can
// activate in schedule order, each with its cost term in entity populations, then
// the root's cascade depth and delayed cycles. Needs `program.ast`.
[[nodiscard]] std::string cost_report(const DecoratedProgram& program);

// The static facts of each handler of the rule with canonical id `rule`; an error
// message when the program has no such rule. Needs `program.ast`.
[[nodiscard]] std::expected<std::string, std::string> explain_rule(const DecoratedProgram& program,
                                                                   std::string_view rule);

}  // namespace cactus
