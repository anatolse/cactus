#pragma once

#include "common/types.hpp"

#include <optional>
#include <string>

namespace cactus {

// Closed (left TypeKind, operator, right TypeKind) -> result TypeKind matrix
// for vec2/vec3 binary operators (dsl-vector-expressions spec). No ranking,
// no ambiguity, no dot product on `*` - every accepted combination is an
// exact row here. Shared by the general semantic analyzer's BinaryExpr type
// inference and compound-assignment validation, and by the cpp-entt
// backend's render-pass GLSL stage-handler translator, so both stay
// consistent by construction.
std::optional<TypeKind> lookup_vector_binary_op_result(TypeKind left, const std::string& op, TypeKind right);

}  // namespace cactus
