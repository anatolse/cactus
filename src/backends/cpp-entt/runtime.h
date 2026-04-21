#pragma once

#include "common/cactus_runtime.h"

#include <array>
#include <entt/entt.hpp>
#include <functional>

namespace cactus::runtime::entt_backend {

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
    int active_point_lights{0};
    int missing_assets{0};
    bool used_default_2d_camera{false};
    bool used_default_3d_camera{false};
    bool used_lit_mesh_shader{false};
    std::vector<int> drawn_sprite_layers{};
};

struct ProjectConfig {
    int window_width;
    int window_height;
    const char* window_title;
    int target_fps;
};

[[nodiscard]] RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept;
void reset_render_debug_state() noexcept;
[[nodiscard]] const RenderDebugState& render_debug_state() noexcept;
void begin_render_frame() noexcept;
void end_render_frame() noexcept;

void submit_sprite(Vector2 position,
                   Vector2 size,
                   Color color,
                   AssetHandle texture,
                   bool visible,
                   int layer) noexcept;
void advance_animated_sprite(AssetHandle texture,
                             int& frame,
                             int frame_count,
                             float fps,
                             bool playing,
                             float dt) noexcept;
void submit_mesh(Vector3 position,
                 Quat rotation,
                 Vector3 scale,
                 AssetHandle mesh,
                 AssetHandle material,
                 bool visible,
                 bool cast_shadow) noexcept;
void submit_billboard(Vector3 position,
                      Vector2 size,
                      Color color,
                      AssetHandle texture,
                      bool visible) noexcept;
void register_point_light(Vector3 position,
                          Color color,
                          float intensity,
                          float range,
                          bool enabled) noexcept;
void register_directional_light(Vector3 direction,
                                Color color,
                                float intensity,
                                bool enabled) noexcept;

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent);

void destroy_entity_recursive(entt::registry& registry,
                              entt::entity entity,
                              const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children);

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

[[nodiscard]] inline Vector2 axis2(std::uint8_t x_action, std::uint8_t y_action) noexcept {
    return Vector2{.x = axis(x_action), .y = axis(y_action)};
}

void generated_setup_dispatcher(entt::dispatcher& dispatcher);
void generated_init_project(entt::registry& registry);
void generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt);
void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher);
[[nodiscard]] ProjectConfig generated_project_config() noexcept;

}  // namespace cactus::runtime::entt_backend