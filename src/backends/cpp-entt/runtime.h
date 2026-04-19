#pragma once

#include "common/cactus_runtime.h"

#include <entt/entt.hpp>
#include <functional>

namespace cactus::runtime::entt_backend {

int cactus_input_button_key(std::uint8_t button) noexcept;
float cactus_input_axis_value(std::uint8_t action) noexcept;

struct RuntimeBinding {
    GeneratedProjectInfo project;
};

struct ProjectConfig {
    int window_width;
    int window_height;
    const char* window_title;
    int target_fps;
};

[[nodiscard]] RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept;

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent);

void destroy_entity_recursive(entt::registry& registry,
                              entt::entity entity,
                              const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children);

[[nodiscard]] bool pressed(std::uint8_t button) noexcept;
[[nodiscard]] bool down(std::uint8_t button) noexcept;
[[nodiscard]] bool released(std::uint8_t button) noexcept;
[[nodiscard]] float axis(std::uint8_t action) noexcept;
[[nodiscard]] Vector2 axis2(std::uint8_t x_action, std::uint8_t y_action) noexcept;

void generated_setup_dispatcher(entt::dispatcher& dispatcher);
void generated_init_project(entt::registry& registry);
void generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt);
void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher);
[[nodiscard]] ProjectConfig generated_project_config() noexcept;

}  // namespace cactus::runtime::entt_backend