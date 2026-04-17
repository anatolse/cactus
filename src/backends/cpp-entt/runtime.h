#pragma once

#include "common/cactus_runtime.h"

#include <entt/entt.hpp>

namespace cactus::runtime::entt_backend {

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

void generated_setup_dispatcher(entt::dispatcher& dispatcher);
void generated_init_project(entt::registry& registry);
void generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt);
void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher);
[[nodiscard]] ProjectConfig generated_project_config() noexcept;

}  // namespace cactus::runtime::entt_backend