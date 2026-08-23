#pragma once

#include "common/types.hpp"

#include <string>
#include <vector>

namespace cactus {

// Single source of truth for the `Quads` render-pass kind's fixed built-in
// per-invocation field set (dsl-render-passes design.md Decision 2). Consumed
// by both the frontend (stage-handler read/write type resolution) and the
// cpp-entt backend (GLSL vertex attribute / varying / uniform naming), so the
// two never drift on field names, types, or order.
struct RenderPassBuiltinField {
    std::string name;
    TypeInfo type;
};

[[nodiscard]] inline const std::vector<RenderPassBuiltinField>& quads_vertex_input_fields() {
    static const std::vector<RenderPassBuiltinField> fields = {
        {.name = "corner", .type = make_vec2_type()},
        {.name = "uv", .type = make_vec2_type()},
        {.name = "vertex_index", .type = make_int_type()},
    };
    return fields;
}

[[nodiscard]] inline const std::vector<RenderPassBuiltinField>& quads_vertex_output_fields() {
    static const std::vector<RenderPassBuiltinField> fields = {
        {.name = "screen_position", .type = make_vec2_type()},
        {.name = "uv_out", .type = make_vec2_type()},
        {.name = "tint_out", .type = make_color_type()},
    };
    return fields;
}

[[nodiscard]] inline const std::vector<RenderPassBuiltinField>& quads_fragment_input_fields() {
    static const std::vector<RenderPassBuiltinField> fields = {
        {.name = "uv", .type = make_vec2_type()},
        {.name = "tint", .type = make_color_type()},
        {.name = "frag_coord", .type = make_vec2_type()},
    };
    return fields;
}

[[nodiscard]] inline const std::vector<RenderPassBuiltinField>& quads_fragment_output_fields() {
    static const std::vector<RenderPassBuiltinField> fields = {
        {.name = "frag_color", .type = make_color_type()},
    };
    return fields;
}

}  // namespace cactus
