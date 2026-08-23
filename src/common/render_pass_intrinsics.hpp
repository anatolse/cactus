#pragma once

#include "frontend/symbol_identity.hpp"

namespace cactus {

// Single source of truth for which `extern func`s have a portable GLSL
// translation and are therefore callable from a render-pass stage handler
// body (dsl-render-passes, "Stage handler body is restricted..." /
// backend-cpp-entt, "Portable GLSL translation is an explicit per-function
// registration"). Consulted by both frontend semantic analysis (stage-handler
// call validation) and the cpp-entt backend (GLSL intrinsic call emission) so
// the two never diverge on what is actually registered.
//
// An ordinary (non-extern) `func` is never listed here — Decision 3 allows
// calling any pure `func` from a stage handler unconditionally; the backend
// translates its body directly rather than treating it as a registered
// intrinsic.
[[nodiscard]] inline bool is_render_pass_portable_glsl_intrinsic(const SymbolId& symbol) {
    return symbol == make_symbol_id(SymbolKind::Func, "std.math", "sqrt") ||
           symbol == make_symbol_id(SymbolKind::Func, "std.math", "clamp");
}

}  // namespace cactus
