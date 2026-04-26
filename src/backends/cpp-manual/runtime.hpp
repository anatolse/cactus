#pragma once

#include "common/cactus_runtime.hpp"

#include <cstddef>
#include <functional>
#include <memory_resource>
#include <optional>

namespace cactus::runtime::manual_backend {

int cactus_input_button_key(std::uint8_t button) noexcept;
float cactus_input_axis_value(std::uint8_t action) noexcept;

struct RuntimeBinding {
    GeneratedProjectInfo project;
};

struct RenderDebugState {
    int submitted_sprites{0};
    int advanced_animated_sprites{0};
    int submitted_meshes{0};
    int submitted_billboards{0};
    int registered_point_lights{0};
    int registered_directional_lights{0};
    int missing_assets{0};
};

struct ProjectConfig {
    int window_width;
    int window_height;
    const char* window_title;
    int target_fps;
};

using ParentResolver        = std::function<std::optional<std::size_t>(std::size_t)>;
using CopyLocalTransformFn  = std::function<void(std::size_t)>;
using AccumulateTransformFn = std::function<void(std::size_t parent, std::size_t child)>;
using IsValidEntityFn       = std::function<bool(std::size_t)>;
using VisitChildrenFn = std::function<void(std::size_t parent, const std::function<void(std::size_t child)>& visitor)>;
using RemoveEntityFn  = std::function<void(std::size_t)>;

[[nodiscard]] RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept;
void reset_render_debug_state() noexcept;
[[nodiscard]] const RenderDebugState& render_debug_state() noexcept;

void submit_sprite(Vector2 position, Vector2 size, Color color, AssetHandle texture, bool visible) noexcept;
void advance_animated_sprite(
    AssetHandle texture, int& frame, int frame_count, float fps, bool playing, float dt) noexcept;
void submit_mesh(Vector3 position,
                 Quat rotation,
                 Vector3 scale,
                 AssetHandle mesh,
                 AssetHandle material,
                 bool visible,
                 bool cast_shadow) noexcept;
void submit_billboard(Vector3 position, Vector2 size, Color color, AssetHandle texture, bool visible) noexcept;
void register_point_light(Vector3 position, Color color, float intensity, float range, bool enabled) noexcept;
void register_directional_light(Vector3 direction, Color color, float intensity, bool enabled) noexcept;

void propagate_hierarchy(std::size_t entity_count,
                         const ParentResolver& resolve_parent,
                         const CopyLocalTransformFn& copy_local,
                         const AccumulateTransformFn& accumulate_from_parent);

void destroy_entity_recursive(std::size_t entity,
                              const IsValidEntityFn& is_valid,
                              const VisitChildrenFn& visit_children,
                              const RemoveEntityFn& remove_entity);

[[nodiscard]] inline bool pressed(std::uint8_t button) noexcept {
    return IsKeyPressed(cactus_input_button_key(button));
}

[[nodiscard]] inline bool down(std::uint8_t button) noexcept {
    return IsKeyDown(cactus_input_button_key(button));
}

[[nodiscard]] inline bool released(std::uint8_t button) noexcept {
    return IsKeyReleased(cactus_input_button_key(button));
}

[[nodiscard]] inline float axis(std::uint8_t action) noexcept {
    return cactus_input_axis_value(action);
}

[[nodiscard]] inline Vector2 axis2(std::uint8_t x_axis, std::uint8_t y_axis) noexcept {
    return Vector2{.x = axis(x_axis), .y = axis(y_axis)};
}

void generated_init_project();
void generated_update_project(float dt);
void generated_render_project();
[[nodiscard]] ProjectConfig generated_project_config() noexcept;

}  // namespace cactus::runtime::manual_backend