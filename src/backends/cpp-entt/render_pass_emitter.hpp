#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <optional>
#include <string>

namespace cactus {

// dsl-render-passes: generates the full body of generated_dispatch_phase_<name>
// for a recognized `Quads` render-pass phase — GLSL vertex/fragment shader
// source translated from the phase's stage-handler bodies, embedded as C++
// string literals, plus raylib shader load/cache and a per-matching-entity
// draw call. Returns std::nullopt when `phase` is not a render-pass phase (the
// ordinary per-handler dispatch loop in cpp_entt_codegen.cpp handles that
// case instead).
[[nodiscard]] std::optional<std::string> emit_render_pass_dispatch_body(const DecoratedProgram& program,
                                                                        const ResolvedPhase& phase);

}  // namespace cactus
