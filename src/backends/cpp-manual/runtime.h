#pragma once

#include "common/cactus_runtime.h"

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

struct ProjectConfig {
    int window_width;
    int window_height;
    const char* window_title;
    int target_fps;
};

using ParentResolver = std::function<std::optional<std::size_t>(std::size_t)>;
using CopyLocalTransformFn = std::function<void(std::size_t)>;
using AccumulateTransformFn = std::function<void(std::size_t parent, std::size_t child)>;
using IsValidEntityFn = std::function<bool(std::size_t)>;
using VisitChildrenFn = std::function<void(std::size_t parent,
                                           const std::function<void(std::size_t child)>& visitor)>;
using RemoveEntityFn = std::function<void(std::size_t)>;

[[nodiscard]] RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept;

void propagate_hierarchy(std::size_t entity_count,
                         const ParentResolver& resolve_parent,
                         const CopyLocalTransformFn& copy_local,
                         const AccumulateTransformFn& accumulate_from_parent);

void destroy_entity_recursive(std::size_t entity,
                              const IsValidEntityFn& is_valid,
                              const VisitChildrenFn& visit_children,
                              const RemoveEntityFn& remove_entity);

[[nodiscard]] bool pressed(std::uint8_t button) noexcept;
[[nodiscard]] bool down(std::uint8_t button) noexcept;
[[nodiscard]] bool released(std::uint8_t button) noexcept;
[[nodiscard]] float axis(std::uint8_t axis) noexcept;
[[nodiscard]] Vector2 axis2(std::uint8_t x_axis, std::uint8_t y_axis) noexcept;

void generated_init_project();
void generated_update_project(float dt);
void generated_render_project();
[[nodiscard]] ProjectConfig generated_project_config() noexcept;

}  // namespace cactus::runtime::manual_backend