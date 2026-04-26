#include "backends/cpp-manual/runtime.hpp"

#include <raylib.h>

#include <memory_resource>
#include <vector>

namespace cactus::runtime::manual_backend {

namespace {
RenderDebugState& render_debug_state_storage() noexcept {
    static RenderDebugState state;
    return state;
}

void note_missing_asset() noexcept {
    ++render_debug_state_storage().missing_assets;
}
}  // namespace

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

void reset_render_debug_state() noexcept {
    render_debug_state_storage() = {};
    shared_asset_registry().clear_diagnostics();
}

const RenderDebugState& render_debug_state() noexcept {
    return render_debug_state_storage();
}

void submit_sprite(const Vector2 /*position*/,
                   const Vector2 /*size*/,
                   const Color /*color*/,
                   const AssetHandle texture,
                   const bool visible) noexcept {
    if (!visible) {
        return;
    }
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.ready()) {
        note_missing_asset();
        return;
    }
    ++render_debug_state_storage().submitted_sprites;
}

void advance_animated_sprite(const AssetHandle texture,
                             int& frame,
                             const int frame_count,
                             const float fps,
                             const bool playing,
                             const float dt) noexcept {
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.valid()) {
        note_missing_asset();
        return;
    }
    if (!playing || frame_count <= 0 || fps <= 0.0F) {
        return;
    }
    const auto step = static_cast<int>(dt * fps);
    if (step <= 0) {
        return;
    }
    frame = (frame + step) % frame_count;
    ++render_debug_state_storage().advanced_animated_sprites;
}

void submit_mesh(const Vector3 /*position*/,
                 const Quat /*rotation*/,
                 const Vector3 /*scale*/,
                 const AssetHandle mesh,
                 const AssetHandle material,
                 const bool visible,
                 const bool /*cast_shadow*/) noexcept {
    if (!visible) {
        return;
    }
    const auto mesh_resolved     = shared_asset_registry().resolve(AssetKind::Mesh, mesh);
    const auto material_resolved = shared_asset_registry().resolve(AssetKind::Material, material);
    if (!mesh_resolved.ready() || !material_resolved.ready()) {
        note_missing_asset();
        return;
    }
    ++render_debug_state_storage().submitted_meshes;
}

void submit_billboard(const Vector3 /*position*/,
                      const Vector2 /*size*/,
                      const Color /*color*/,
                      const AssetHandle texture,
                      const bool visible) noexcept {
    if (!visible) {
        return;
    }
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.ready()) {
        note_missing_asset();
        return;
    }
    ++render_debug_state_storage().submitted_billboards;
}

void register_point_light(const Vector3 /*position*/,
                          const Color /*color*/,
                          const float /*intensity*/,
                          const float /*range*/,
                          const bool enabled) noexcept {
    if (enabled) {
        ++render_debug_state_storage().registered_point_lights;
    }
}

void register_directional_light(const Vector3 /*direction*/,
                                const Color /*color*/,
                                const float /*intensity*/,
                                const bool enabled) noexcept {
    if (enabled) {
        ++render_debug_state_storage().registered_directional_lights;
    }
}

void propagate_hierarchy(std::size_t entity_count,
                         const ParentResolver& resolve_parent,
                         const CopyLocalTransformFn& copy_local,
                         const AccumulateTransformFn& accumulate_from_parent) {
    std::pmr::monotonic_buffer_resource scratch_resource;
    std::pmr::vector<std::uint8_t> active(&scratch_resource);
    active.resize(entity_count, 0U);

    const auto resolve = [&](const auto& self, std::size_t entity) -> void {
        if (entity >= entity_count || active[entity] != 0U) {
            return;
        }

        active[entity]    = 1U;
        bool copied_local = false;

        if (const auto parent = resolve_parent(entity); parent.has_value() && parent.value() < entity_count) {
            self(self, parent.value());
            accumulate_from_parent(parent.value(), entity);
            copied_local = true;
        }

        if (!copied_local) {
            copy_local(entity);
        }

        active[entity] = 0U;
    };

    for (std::size_t entity = 0; entity < entity_count; ++entity) {
        resolve(resolve, entity);
    }
}

void destroy_entity_recursive(std::size_t entity,
                              const IsValidEntityFn& is_valid,
                              const VisitChildrenFn& visit_children,
                              const RemoveEntityFn& remove_entity) {
    static std::pmr::unsynchronized_pool_resource destroying_resource;
    static std::pmr::vector<std::size_t> destroying_entities{&destroying_resource};

    for (const auto active_entity : destroying_entities) {
        if (active_entity == entity) {
            return;
        }
    }

    if (!is_valid(entity)) {
        return;
    }

    destroying_entities.push_back(entity);
    visit_children(
        entity, [&](std::size_t child) { destroy_entity_recursive(child, is_valid, visit_children, remove_entity); });

    if (is_valid(entity)) {
        remove_entity(entity);
    }

    if (!destroying_entities.empty()) {
        destroying_entities.pop_back();
    }
}

}  // namespace cactus::runtime::manual_backend