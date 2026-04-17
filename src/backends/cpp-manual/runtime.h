#pragma once

#include "common/cactus_runtime.h"

#include <cstddef>
#include <functional>
#include <optional>

namespace cactus::runtime::manual_backend {

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

void generated_init_project();
void generated_update_project(float dt);
void generated_render_project();
[[nodiscard]] ProjectConfig generated_project_config() noexcept;

}  // namespace cactus::runtime::manual_backend