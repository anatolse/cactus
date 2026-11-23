#pragma once

#include "frontend/symbol_identity.hpp"

namespace cactus {

// The `extern func`s a drawing rule's stage handler may call: each has a GLSL
// built-in of the same name and signature. Shared by the frontend (call checks)
// and the cpp-entt backend (GLSL emission). A pure `func` is never listed; the
// backend translates its body.
[[nodiscard]] inline bool is_portable_glsl_intrinsic(const SymbolId& symbol) {
    return symbol == make_symbol_id(SymbolKind::Func, "std.math", "sqrt") ||
           symbol == make_symbol_id(SymbolKind::Func, "std.math", "clamp") ||
           symbol == make_symbol_id(SymbolKind::Func, "std.math", "radians") ||
           symbol == make_symbol_id(SymbolKind::Func, "std.math", "degrees");
}

}  // namespace cactus
