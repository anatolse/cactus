#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

// Both stages as one GLSL shader pair, the values they read set as uniforms, and one quad per row.
[[nodiscard]] std::string emit_drawing_rule_function(const RuleNode& rule, const DecoratedProgram& program);

[[nodiscard]] std::string drawing_rule_callee(const DecoratedProgram& program, const SymbolId& rule);

// Scheduled after the UI or screen labels, so its draw is deferred to the end of the frame.
[[nodiscard]] bool draws_after_window_space(const DecoratedProgram& program, const SymbolId& rule);

}  // namespace cactus
