#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <functional>
#include <string>
#include <vector>

namespace cactus {

struct StageTranslationLookups {
    std::function<const ResolvedFunc*(const SymbolId&)> func;
    std::function<const ResolvedConst*(const SymbolId&)> constant;
    std::function<const ResolvedStruct*(const SymbolId&)> structure;
    // The canonical id of the asset `read` names; empty when it names none.
    std::function<std::string(const ExprNode& read)> asset;
};

// `vec2(...)`, `vec3(...)` or `color(...)`: a value a shader builds itself.
[[nodiscard]] bool is_shader_value_constructor(const CallExpr& call);

// An extern with a shader translation, or a func whose body a shader can run.
[[nodiscard]] bool stage_may_call(const ResolvedFunc& func, const SymbolId& symbol);

// The arguments of `call` a `texture_id` parameter takes: the only places a stage may use a texture.
[[nodiscard]] std::vector<const ExprNode*> texture_arguments(const CallExpr& call, const ResolvedFunc& callee);

// The fragment-only extern `node` calls, directly or through funcs already marked; empty when none.
[[nodiscard]] std::string fragment_only_call(const FuncNode& node, const StageTranslationLookups& lookups);

// `range(a, b)` or `range(a, b, step)` whose arguments are built only from literals and constants.
[[nodiscard]] bool is_constant_range(const ExprNode& iterable);

// Why a stage handler can't call the non-extern `node`, from its own body and the
// facts already known about its callees; empty when it can.
[[nodiscard]] std::string stage_func_blocker(const FuncNode& node,
                                             const ResolvedFunc& func,
                                             const StageTranslationLookups& lookups);

}  // namespace cactus
