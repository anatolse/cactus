#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <functional>
#include <string>

namespace cactus {

struct StageTranslationLookups {
    std::function<const ResolvedFunc*(const SymbolId&)> func;
    std::function<const ResolvedConst*(const SymbolId&)> constant;
    std::function<const ResolvedStruct*(const SymbolId&)> structure;
};

// `vec2(...)`, `vec3(...)` or `color(...)`: a value a shader builds itself.
[[nodiscard]] bool is_shader_value_constructor(const CallExpr& call);

// An extern with a shader translation, or a func whose body a shader can run.
[[nodiscard]] bool stage_may_call(const ResolvedFunc& func, const SymbolId& symbol);

// `range(a, b)` or `range(a, b, step)` whose arguments are built only from literals and constants.
[[nodiscard]] bool is_constant_range(const ExprNode& iterable);

// Why a stage handler can't call the non-extern `node`, from its own body and the
// facts already known about its callees; empty when it can.
[[nodiscard]] std::string stage_func_blocker(const FuncNode& node,
                                             const ResolvedFunc& func,
                                             const StageTranslationLookups& lookups);

}  // namespace cactus
