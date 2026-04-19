#include "backends/cpp-entt/runtime.h"

#include <raylib.h>

#include <memory_resource>

namespace cactus::runtime::entt_backend {

namespace {
RenderDebugState g_render_debug_state;

void note_missing_asset() noexcept {
    ++g_render_debug_state.missing_assets;
}
}  // namespace

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

void reset_render_debug_state() noexcept {
    g_render_debug_state = {};
    shared_asset_registry().clear_diagnostics();
}

const RenderDebugState& render_debug_state() noexcept {
    return g_render_debug_state;
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
    ++g_render_debug_state.submitted_sprites;
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
    ++g_render_debug_state.advanced_animated_sprites;
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
    const auto mesh_resolved = shared_asset_registry().resolve(AssetKind::Mesh, mesh);
    const auto material_resolved = shared_asset_registry().resolve(AssetKind::Material, material);
    if (!mesh_resolved.ready() || !material_resolved.ready()) {
        note_missing_asset();
        return;
    }
    ++g_render_debug_state.submitted_meshes;
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
    ++g_render_debug_state.submitted_billboards;
}

void register_point_light(const Vector3 /*position*/,
                          const Color /*color*/,
                          const float /*intensity*/,
                          const float /*range*/,
                          const bool enabled) noexcept {
    if (enabled) {
        ++g_render_debug_state.registered_point_lights;
    }
}

void register_directional_light(const Vector3 /*direction*/,
                                const Color /*color*/,
                                const float /*intensity*/,
                                const bool enabled) noexcept {
    if (enabled) {
        ++g_render_debug_state.registered_directional_lights;
    }
}

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent) {
    std::pmr::monotonic_buffer_resource scratch_resource;
    std::pmr::vector<entt::entity> active_entities{&scratch_resource};

    std::function<void(entt::entity)> resolve;
    resolve = [&](entt::entity entity) -> void {
        const bool already_active = std::find(active_entities.begin(), active_entities.end(), entity) != active_entities.end();
        if (!registry.valid(entity) || already_active || !has_local_world(entity)) {
            return;
        }

        active_entities.push_back(entity);
        bool copied_local = false;
        const entt::entity parent = get_parent(entity);
        if (parent != entt::null && registry.valid(parent) && has_local_world(parent)) {
            resolve(parent);
            accumulate_from_parent(parent, entity);
            copied_local = true;
        } else {
            copy_local(entity);
        }
        if (!copied_local) {
            copy_local(entity);
        }
        if (!active_entities.empty()) {
            active_entities.pop_back();
        }
    };

    auto& storage = registry.storage<entt::entity>();
    for (const auto entry : storage.each()) {
        resolve(std::get<0>(entry));
    }
}

void destroy_entity_recursive(entt::registry& registry,
                              entt::entity entity,
                              const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children) {
    static std::pmr::unsynchronized_pool_resource destroying_resource;
    static std::pmr::vector<entt::entity> destroying_entities{&destroying_resource};
    const bool already_destroying = std::find(destroying_entities.begin(), destroying_entities.end(), entity) != destroying_entities.end();
    if (!registry.valid(entity) || already_destroying) {
        return;
    }

    destroying_entities.push_back(entity);
    std::pmr::monotonic_buffer_resource child_resource;
    std::pmr::vector<entt::entity> child_entities{&child_resource};
    visit_children(entity, [&](entt::entity child) { child_entities.push_back(child); });
    for (const auto child : child_entities) {
        destroy_entity_recursive(registry, child, visit_children);
    }
    if (registry.valid(entity)) {
        registry.destroy(entity);
    }
    if (!destroying_entities.empty()) {
        destroying_entities.pop_back();
    }
}

}  // namespace cactus::runtime::entt_backend