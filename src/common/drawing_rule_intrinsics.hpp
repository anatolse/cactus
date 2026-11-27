#pragma once

#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace cactus {

// The `extern func`s a drawing rule's stage handler may call, by module. Shared
// by the frontend (call checks) and the cpp-entt backend (GLSL emission), which
// translates each one. A pure `func` is never listed; the backend translates its body.
inline constexpr auto kStageMathExterns = std::to_array<std::string_view>(
    {"lerp", "clamp", "abs", "min", "max", "sqrt", "sin", "cos", "radians", "degrees", "atan2", "floor", "ceil", "round",
     "pow"});
inline constexpr auto kStageVec2Externs = std::to_array<std::string_view>(
    {"length", "normalize", "dot", "lerp", "distance", "angle", "clamp", "rotate"});
inline constexpr auto kStageVec3Externs = std::to_array<std::string_view>(
    {"length", "normalize", "dot", "cross", "lerp", "distance", "reflect", "clamp", "rotate"});

[[nodiscard]] inline std::span<const std::string_view> stage_externs_of(std::string_view module) {
    if (module == "std.math") {
        return kStageMathExterns;
    }
    if (module == "std.math.vec2") {
        return kStageVec2Externs;
    }
    if (module == "std.math.vec3") {
        return kStageVec3Externs;
    }
    return {};
}

[[nodiscard]] inline bool is_portable_glsl_intrinsic(const SymbolId& symbol) {
    return symbol.kind == SymbolKind::Func && std::ranges::contains(stage_externs_of(symbol.module.name), symbol.local_name);
}

}  // namespace cactus
