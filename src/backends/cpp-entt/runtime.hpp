#pragma once

#include "common/cactus_runtime.hpp"

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace cactus::runtime::entt_backend {

int cactus_input_button_key(std::uint8_t button) noexcept;
int cactus_input_button_mouse(std::uint8_t button) noexcept;
float cactus_input_axis_value(std::uint8_t action) noexcept;

// ── Active camera state (set once per frame before any camera-dependent code) ──
void set_active_camera_2d(Camera2D cam) noexcept;
[[nodiscard]] Camera2D get_active_camera_2d() noexcept;
void set_active_camera_3d(Camera3D cam) noexcept;
[[nodiscard]] Camera3D get_active_camera_3d() noexcept;

struct RuntimeBinding {
    GeneratedProjectInfo project;
};

struct RenderDebugState {
    struct AnimatedModelSubmission {
        int clip{0};
        float time{0.0F};
    };

    int submitted_sprites{0};
    int advanced_animated_sprites{0};
    int submitted_meshes{0};
    int submitted_models{0};
    int drawn_models{0};
    int submitted_billboards{0};
    int submitted_screen_labels{0};
    int registered_point_lights{0};
    int registered_directional_lights{0};
    int active_point_lights{0};
    int missing_assets{0};
    bool used_default_2d_camera{false};
    bool used_default_3d_camera{false};
    bool used_lit_mesh_shader{false};
    std::vector<int> drawn_sprite_layers;
    // (clip, time) of each animated model submission this frame, in submission
    // order — the per-entity poses observable without a GPU.
    std::vector<AnimatedModelSubmission> animated_model_submissions;
    // Missing/failed model and invalid-animation-clip diagnostics, recorded at
    // most once per model asset (or per (asset, clip) pair).
    std::vector<std::string> model_diagnostics;
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
// Draws and clears the queued 3D meshes, sprites, and 2D text immediately,
// using the currently active cameras. Called once per viewport so each
// split-screen region renders within its scissor rect.
void flush_viewport_3d() noexcept;

void submit_sprite(Vector2 position, Vector2 size, Color color, AssetHandle texture, bool visible, int layer) noexcept;
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
void submit_model(Vector3 position,
                  Quat rotation,
                  Vector3 scale,
                  AssetHandle model,
                  bool visible,
                  bool cast_shadow) noexcept;
// Animated variant: carries the entity's ModelAnimator (clip, time) so the
// flush re-poses the shared model per submission (dsl-model-animation).
void submit_model(Vector3 position,
                  Quat rotation,
                  Vector3 scale,
                  AssetHandle model,
                  bool visible,
                  bool cast_shadow,
                  int clip,
                  float time) noexcept;

// ── Animation introspection extern func bridges (std.render.models) ───────────

/// Number of animation clips in the model; 0 for an unresolvable handle.
/// Triggers the model's lazy load, so it works before the first draw.
[[nodiscard]] int model_animation_count(AssetHandle model) noexcept;

/// Clip name as stored in the model file; "" for a bad handle or index.
[[nodiscard]] std::string model_animation_name(AssetHandle model, int clip) noexcept;
void submit_billboard(Vector3 position, Vector2 size, Color color, AssetHandle texture, bool visible) noexcept;
void register_point_light(Vector3 position, Color color, float intensity, float range, bool enabled) noexcept;
void register_directional_light(Vector3 direction, Color color, float intensity, bool enabled) noexcept;
void submit_text_2d(Vector2 position,
                    float rotation_rad,
                    int font_size,
                    Color color,
                    const std::string& text,
                    bool visible) noexcept;
// Window-space HUD label (std.render.text ScreenLabel): window-global pixel
// coordinates with top-left origin, drawn after all viewport/world rendering.
void submit_screen_label(Vector2 position,
                         int font_size,
                         Color color,
                         const std::string& text,
                         bool visible) noexcept;
void submit_text_3d(std::uint32_t entity_id,
                    Vector3 position,
                    Quat rotation,
                    Vector3 scale,
                    int font_size,
                    Color color,
                    const std::string& text,
                    bool visible) noexcept;

// ── Editor extern func bridges (std.editor) ───────────────────────────────────

/// Spawn a template entity by name at the given 2D/3D position.
/// Returns the created entity handle, or entt::null on failure.
[[nodiscard]] entt::entity editor_spawn_template(entt::registry& registry,
                                                 const std::string& template_name,
                                                 Vector2 position_2d,
                                                 Vector3 position_3d) noexcept;

/// 2D hit-test: return the top-most entity under screen_pos matching mask, or entt::null.
[[nodiscard]] entt::entity editor_hit_test_2d(entt::registry& registry,
                                              Vector2 screen_pos,
                                              int mask) noexcept;

// ── Editor impl registration (called by generated_init_project) ───────────────
using EditorHitTestImpl = std::function<entt::entity(entt::registry&, Vector2, int)>;
using EditorSpawnImpl =
    std::function<entt::entity(entt::registry&, const std::string&, Vector2, Vector3)>;
void register_editor_hit_test_impl(EditorHitTestImpl fn) noexcept;
void register_editor_spawn_impl(EditorSpawnImpl fn) noexcept;

/// 3D raycast: return the first entity hit by a ray from screen_pos, or entt::null.
[[nodiscard]] entt::entity editor_raycast_3d(Vector2 screen_pos, int mask) noexcept;

/// Convert a screen position to a 2D world position.
[[nodiscard]] Vector2 editor_screen_to_world_2d(Vector2 screen) noexcept;

/// Return the current frame's mouse delta in 2D world space.
[[nodiscard]] Vector2 editor_mouse_delta_2d() noexcept;

/// Project a screen position onto a 3D plane.
[[nodiscard]] Vector3 editor_plane_project_3d(Vector2 screen, Vector3 plane_origin, Vector3 plane_normal) noexcept;

/// Return the current frame's mouse delta in 3D world space.
[[nodiscard]] Vector3 editor_mouse_delta_3d() noexcept;

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent);

void destroy_entity_recursive(
    entt::registry& registry,
    entt::entity entity,
    const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children);

[[nodiscard]] inline bool pressed(std::uint8_t button) noexcept {
    const int mouse_button = cactus_input_button_mouse(button);
    if (mouse_button >= 0) {
        return IsMouseButtonPressed(mouse_button);
    }
    const int key = cactus_input_button_key(button);
    return key != 0 && IsKeyPressed(key);
}

[[nodiscard]] inline bool down(std::uint8_t button) noexcept {
    const int mouse_button = cactus_input_button_mouse(button);
    if (mouse_button >= 0) {
        return IsMouseButtonDown(mouse_button);
    }
    const int key = cactus_input_button_key(button);
    return key != 0 && IsKeyDown(key);
}

[[nodiscard]] inline bool released(std::uint8_t button) noexcept {
    const int mouse_button = cactus_input_button_mouse(button);
    if (mouse_button >= 0) {
        return IsMouseButtonReleased(mouse_button);
    }
    const int key = cactus_input_button_key(button);
    return key != 0 && IsKeyReleased(key);
}

[[nodiscard]] inline float axis(std::uint8_t action) noexcept {
    return cactus_input_axis_value(action);
}

[[nodiscard]] inline Vector2 axis2(std::uint8_t x_action, std::uint8_t y_action) noexcept {
    return Vector2{.x = axis(x_action), .y = axis(y_action)};
}

[[nodiscard]] inline Vector2 mouse_position() noexcept {
    return GetMousePosition();
}

void generated_setup_dispatcher(entt::dispatcher& dispatcher);
void generated_init_project(entt::registry& registry);
void generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt);
void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher);
[[nodiscard]] ProjectConfig generated_project_config() noexcept;

}  // namespace cactus::runtime::entt_backend