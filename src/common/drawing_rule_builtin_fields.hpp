#pragma once

#include "common/types.hpp"

#include <string>
#include <vector>

namespace cactus {

// A value a shader holds.
[[nodiscard]] inline bool is_glsl_value_type(TypeKind kind) {
    switch (kind) {
        case TypeKind::Int:
        case TypeKind::Float:
        case TypeKind::Bool:
        case TypeKind::Vec2:
        case TypeKind::Vec3:
        case TypeKind::Color:
        case TypeKind::Enum:
            return true;
        default:
            return false;
    }
}

// Shared by the frontend and the cpp-entt backend so the two never drift.
struct DrawingRuleBuiltinField {
    std::string name;
    TypeInfo type;
};

[[nodiscard]] inline const std::vector<DrawingRuleBuiltinField>& drawing_vertex_input_fields() {
    static const std::vector<DrawingRuleBuiltinField> fields = {
        {.name = "corner", .type = make_vec2_type()},
        {.name = "uv", .type = make_vec2_type()},
        {.name = "vertex_index", .type = make_int_type()},
    };
    return fields;
}

[[nodiscard]] inline const std::vector<DrawingRuleBuiltinField>& drawing_vertex_output_fields() {
    static const std::vector<DrawingRuleBuiltinField> fields = {
        {.name = "world_position", .type = make_vec2_type()},
        {.name = "screen_position", .type = make_vec2_type()},
        {.name = "uv_out", .type = make_vec2_type()},
        {.name = "tint_out", .type = make_color_type()},
    };
    return fields;
}

[[nodiscard]] inline const std::vector<DrawingRuleBuiltinField>& drawing_fragment_input_fields() {
    static const std::vector<DrawingRuleBuiltinField> fields = {
        {.name = "uv", .type = make_vec2_type()},
        {.name = "tint", .type = make_color_type()},
        {.name = "frag_coord", .type = make_vec2_type()},
    };
    return fields;
}

[[nodiscard]] inline const std::vector<DrawingRuleBuiltinField>& drawing_fragment_output_fields() {
    static const std::vector<DrawingRuleBuiltinField> fields = {
        {.name = "frag_color", .type = make_color_type()},
        {.name = "light", .type = make_color_type()},
    };
    return fields;
}

}  // namespace cactus
