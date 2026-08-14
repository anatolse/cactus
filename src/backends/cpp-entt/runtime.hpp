#pragma once

#include "common/cactus_runtime.hpp"

#include "backends/cpp-entt/raylib_io.hpp"
#include "backends/cpp-entt/spatial_query.hpp"

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cactus::runtime::entt_backend {

int cactus_input_button_key(std::uint8_t button) noexcept;
int cactus_input_button_mouse(std::uint8_t button) noexcept;
float cactus_input_axis_value(std::uint8_t action) noexcept;

// ── Frame-local consumed input (editor input override) ────────────────────────
// Consumption is keyed by physical raylib key / mouse button code, not by
// declared action, so consuming an editor control also hides same-key gameplay
// bindings. Reset at the top of every generated update frame.
void reset_consumed_input() noexcept;
void mark_input_key_consumed(int key) noexcept;
void mark_input_mouse_consumed(int mouse_button) noexcept;
[[nodiscard]] bool is_input_key_consumed(int key) noexcept;
[[nodiscard]] bool is_input_mouse_consumed(int mouse_button) noexcept;

// ── Active camera state (set once per frame before any camera-dependent code) ──
void set_active_camera_2d(Camera2D cam) noexcept;
[[nodiscard]] Camera2D get_active_camera_2d() noexcept;
void set_active_camera_3d(Camera3D cam) noexcept;
[[nodiscard]] Camera3D get_active_camera_3d() noexcept;

// ── Shape rendering (std.render.shapes ShapeRenderer) ──────────────────────────
// origin is the offset from position to the shape's un-rotated reference
// point (top-left for a rectangle, center for a circle); rotation_rad pivots
// a rectangle around position and is converted to degrees for raylib here so
// generated code never carries that conversion.
void draw_shape_rectangle(Vector2 position, Vector2 size, Vector2 origin, float rotation_rad, Color color) noexcept;
void draw_shape_circle(Vector2 position, float diameter, Vector2 origin, Color color) noexcept;

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
    int submitted_ui_primitives{0};
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
// Draws and clears the queued 3D meshes/models/text, sprites, and 2D text
// immediately, using the currently active cameras. Called once per viewport so
// each split-screen region renders within its scissor rect. Screen labels are
// deliberately excluded — they are window-global and flush in end_render_frame.
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

/// Clip duration in seconds (keyframes / glTF sampling rate); 0 for a bad
/// handle or index. The generated ModelAnimation rule wraps time by this.
[[nodiscard]] float model_animation_duration(AssetHandle model, int clip) noexcept;

/// Bind-pose AABB extents (max − min per axis) of the model. Triggers the
/// model's lazy load, so it works before the first draw. Returns zero extents
/// for an unresolvable handle, a failed load, or before the window exists.
[[nodiscard]] Vector3 model_bounds_size(AssetHandle model) noexcept;
// Bind-pose axis-aligned bounding box of a model asset (min/max corners in
// model space). Triggers the lazy model load like model_bounds_size; returns a
// zero-extent box at the origin for an unresolvable handle or failed load.
[[nodiscard]] BoundingBox model_bounds_box(AssetHandle model) noexcept;
// Midpoint of the bind-pose AABB ((min + max) / 2) in model space — the offset
// a caller must add (scaled) to an entity's world position to center a wire
// box on the actual mesh rather than the entity origin, for models whose
// bind-pose bounds aren't centered on the origin. Same fallback/load behavior
// as model_bounds_size/model_bounds_box.
[[nodiscard]] Vector3 model_bounds_center(AssetHandle model) noexcept;
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
void submit_screen_label(Vector2 position, int font_size, Color color, const std::string& text, bool visible) noexcept;
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
[[nodiscard]] entt::entity editor_hit_test_2d(entt::registry& registry, Vector2 screen_pos, int mask) noexcept;

/// Current EditorState.mode of the singleton Editor entity (0 if none exists). Narrow
/// singleton-read accessor mirroring world_position(of:) — for rules (e.g. the gizmo
/// renderers) that are not filtered on EditorState itself and have no other way to read it.
[[nodiscard]] int editor_active_mode(entt::registry& registry) noexcept;

/// Current EditorState.active of the singleton Editor entity (false if none exists). Same
/// narrow-accessor idiom as editor_active_mode — needed because EditorSelected persists across
/// an editor deactivate (only a selection change clears it), so a rule filtered on
/// EditorSelected but not EditorState (e.g. the gizmo renderers) has no other way to detect
/// "editor is off" and stop drawing.
[[nodiscard]] bool editor_is_active(entt::registry& registry) noexcept;

/// Current window/render-target size in screen pixels.
[[nodiscard]] Vector2 editor_screen_size() noexcept;

// ── Standard UI metric fact bridges (std.ui) ───────────────────────────────

/// Current window rectangle size; {0,0} when no window exists yet.
[[nodiscard]] Vector2 ui_window_size() noexcept;
/// Intrinsic size of `value` set in `font_size`, via the default font.
[[nodiscard]] Vector2 ui_text_size(const std::string& value, int font_size) noexcept;
/// Intrinsic size of a resolved texture asset; {0,0} for an unresolved handle.
[[nodiscard]] Vector2 ui_texture_size(AssetHandle texture) noexcept;

// ── Standard UI unified painter (std.ui RenderUi) ──────────────────────────
// Design decision #10: one recognized external rule reads Node/Visual/
// ComputedLayout plus whichever visual traits an entity carries and performs
// one ordered draw pass; generated code (system_emitter.cpp's
// is_standard_ui_render body) gathers each entity's data into this struct
// (presence flags stand in for std::optional since generated code only does
// plain field assignment) and calls render_ui_primitive in ascending
// ComputedLayout.draw_order, already the complete window-space painter order
// (design decision #7) — no additional traversal or clip-stack is needed
// here because ComputedLayout.clip_min/max already holds each entity's fully
// intersected ancestor clip rectangle.
struct UiPrimitive {
    Vector2 position{};
    Vector2 size{};
    bool effective_visible{true};
    bool effective_enabled{true};
    float effective_opacity{1.0F};
    Vector2 clip_min{};
    Vector2 clip_max{};
    Vector2 visual_scale{.x = 1.0F, .y = 1.0F};

    bool has_panel{false};
    Color panel_background{};
    Color panel_border_color{};
    float panel_border_width{0.0F};

    bool has_text{false};
    std::string text_value;
    int text_font_size{16};
    Color text_color{};
    int text_align{0};  // std_ui__TextAlign ordinal: Start=0, Center=1, End=2

    bool has_image{false};
    AssetHandle image_texture{0};
    Color image_tint{};
    int image_fit{0};  // std_ui__ImageFit ordinal: Stretch=0, Contain=1, Cover=2

    bool has_frame_animation{false};
    int frame_count{1};
    int frame{0};

    bool has_button{false};
    std::string button_label;
    Color button_normal_color{};
    Color button_hover_color{};
    Color button_pressed_color{};
    Color button_disabled_color{};
    Color button_text_color{};

    // std.pointer.PointerState, when present (design decision: "Button
    // presentation derives from generic pointer state"). Absent (both false)
    // until section 8/9's pointer router exists to populate it.
    bool pointer_hovered{false};
    bool pointer_pressed{false};
};

/// Standard primitive order within one entity: background, image, button
/// fill, text-or-button-label, then border (spec.md "one ordered window-space
/// painter pass"). Skips entirely when !effective_visible or the effective
/// clip rectangle is degenerate (fully clipped away).
void render_ui_primitive(const UiPrimitive& primitive) noexcept;

struct ImageDrawRects {
    Rectangle source;
    Rectangle dest;
};

/// Pure fit-mode geometry (Stretch/Contain/Cover) for drawing a
/// `texture_size`-sized horizontal filmstrip frame into a `dest_position`/
/// `dest_size` box. Exposed standalone so fit/frame math is unit-testable
/// without a raylib window; `fit` is a std_ui__ImageFit ordinal (Stretch=0,
/// Contain=1, Cover=2, matching UiPrimitive::image_fit).
[[nodiscard]] ImageDrawRects compute_image_draw_rects(int fit,
                                                       Vector2 dest_position,
                                                       Vector2 dest_size,
                                                       Vector2 texture_size,
                                                       int frame_index,
                                                       int frame_count) noexcept;

// ── Generic pointer picking (std.pointer) ──────────────────────────────────
// Design decision #8: one merged candidate lifecycle spans window UI,
// flat-world, and volume-world entities. Generated code (system_emitter.cpp's
// pointer-query lowering) gathers each domain's candidates — using the
// program's real generated component types, which this shared, program-
// agnostic runtime cannot reference directly — into this plain struct, in
// each domain's own geometric order; the pure functions below own the
// deterministic sort-within-domain and front-to-back blocking rules so they
// are unit-testable without any ECS or generated code involved.
struct PointerCandidate {
    entt::entity entity{entt::null};
    bool enabled{true};
    bool blocks_lower{true};
    int priority{0};
    int draw_order{0};
    float distance{0.0F};
    std::uint64_t creation_ordinal{0};
};

/// Window candidates paint in ascending ComputedLayout.draw_order, so the
/// pointer router considers them in reverse (spec.md "greater computed draw
/// order is considered first").
void sort_window_pointer_candidates(std::vector<PointerCandidate>& candidates) noexcept;

/// PointerTarget.priority descending, then stable creation ordinal ascending
/// ("priority followed by stable creation ordinal" — no standard 2D world
/// painter depth exists yet).
void sort_flat_world_pointer_candidates(std::vector<PointerCandidate>& candidates) noexcept;

/// Nearest positive ray distance first. Callers must have already filtered
/// out non-positive distances (behind the ray origin).
void sort_volume_world_pointer_candidates(std::vector<PointerCandidate>& candidates) noexcept;

/// Walks an already-domain-ordered, concatenated candidate list (window
/// candidates first, then world) front-to-back: the first enabled candidate
/// is selected; a disabled candidate with blocks_lower stops evaluation
/// (nothing is selected); a disabled, nonblocking candidate is skipped.
/// Returns entt::null when no candidate is selected.
[[nodiscard]] entt::entity resolve_pointer_target(const std::vector<PointerCandidate>& ordered_candidates) noexcept;

/// True when `point` (already in the same space as `rect_min`/`rect_max`)
/// falls within the closed rectangle — used for window-space hit testing
/// against a ComputedLayout bounds/clip rectangle.
[[nodiscard]] bool point_in_rect(Vector2 point, Vector2 rect_min, Vector2 rect_max) noexcept;

/// True when `point` falls within an axis-aligned box collider of `size`
/// centered at `center` (std.physics.flat.BoxCollider).
[[nodiscard]] bool point_in_flat_box(Vector2 point, Vector2 center, Vector2 size) noexcept;

/// True when `point` falls within a circle collider (std.physics.flat.CircleCollider).
[[nodiscard]] bool point_in_flat_circle(Vector2 point, Vector2 center, float radius) noexcept;

// ── Pointer candidate gathering (registered by generated_init_project, like
//    the editor hit-test/raycast impls above) ───────────────────────────────
// Each impl gathers its domain's PointerCandidate list (already sorted by
// that domain's own rule) from the real, program-specific component types,
// which this shared runtime cannot reference directly. Absent impls (a
// program that never uses window UI or world pointer targets) contribute an
// empty list, not an error.
using PointerCandidatesImpl = std::function<std::vector<PointerCandidate>(entt::registry&, Vector2)>;
void register_pointer_window_candidates_impl(PointerCandidatesImpl fn) noexcept;
void register_pointer_world_candidates_impl(PointerCandidatesImpl fn) noexcept;

/// std.pointer.top_target(): merges window candidates (reverse painter
/// order) before world candidates (flat priority/ordinal or volume nearest
/// distance, whichever the program uses) at the current mouse position, and
/// resolves the first accepted target under front-to-back blocking. Returns
/// entt::null when nothing accepts.
[[nodiscard]] entt::entity pointer_top_target(entt::registry& registry) noexcept;

// ── Pointer router (std.ui.RoutePointer, design decisions #8/#9) ───────────
// The decisions for one frame — hover/capture transitions, Click validity,
// and whether the primary mouse action should be consumed — computed here as
// one pure-ish step (registry + this frame's mouse edge state in, a plain
// decision struct out) so it's unit-testable without any generated code.
// Generated code (system_emitter.cpp's is_pointer_router body) turns the
// decisions into real component writes and typed targeted event emission,
// which need the program's real generated types this shared runtime cannot
// reference. Hover/capture identity persists across frames in this TU's
// static storage, reset only by reset_pointer_router_state (headless tests).
struct PointerFrameTransitions {
    entt::entity top{entt::null};

    bool hover_changed{false};
    entt::entity leave_target{entt::null};  // valid only if hover_changed && the former hover was live
    entt::entity enter_target{entt::null};  // valid only if hover_changed && the new top is live

    bool press_occurred{false};
    entt::entity press_target{entt::null};

    bool release_occurred{false};
    entt::entity release_target{entt::null};
    bool release_is_click{false};

    // True when this frame's accepted press, active capture, or handled
    // release should consume the primary logical mouse action (spec.md
    // "accepted pointer actions consume their logical input"); false on a
    // miss, so gameplay input elsewhere in the frame stays unaffected.
    bool should_consume_primary{false};
};

[[nodiscard]] PointerFrameTransitions compute_pointer_frame_transitions(entt::registry& registry) noexcept;

/// Clears hover/capture identity between frames/tests. Real programs never
/// need this (state should simply persist); headless tests call it the same
/// way they call cactus_raylib_fake::reset() for raylib's scripted state.
void reset_pointer_router_state() noexcept;

/// Cycles a small fixed 6-color palette by index % 6, for index-based palette
/// button tinting (editor-declarative-rendering design decision 6). Computed
/// here rather than as a DSL if-chain assigning into a `let`-bound local:
/// VarAssign to a plain local inside a nested `if`/`for` block always
/// redeclares with `auto` instead of mutating the outer binding (confirmed
/// codegen bug — see the design notes on task 1.1's foreach-mutation finding,
/// which is the same root cause, just also reachable via `if`, not only
/// `for`), so a DSL `let color = <default>` followed by `if ...: color =
/// ...` branches would silently keep the first-declared value.
[[nodiscard]] Color editor_palette_color(int index) noexcept;

/// GizmoMode name (SELECT/TRANSLATE/ROTATE/SCALE/PLACE) for the HUD overlay's mode text.
/// Same reason this is a backend accessor and not a DSL if-chain as editor_palette_color.
[[nodiscard]] std::string editor_mode_label(int mode) noexcept;

/// Y screen position (pixels) for the index'th palette button: 40 + index * 30 (140x26px
/// buttons, 4px gap). Computed here rather than DSL `40.0 + (idx * 30.0)`: the DSL has no
/// explicit int-to-float cast, so the generated C++ promotes `idx` implicitly, which
/// clang-tidy's narrowing-conversion check (correctly, if pedantically) flags — the explicit
/// cast here is exception-free and warning-free.
[[nodiscard]] float editor_palette_button_y(int index) noexcept;

// ── Editor impl registration (called by generated_init_project) ───────────────
using EditorHitTestImpl    = std::function<entt::entity(entt::registry&, Vector2, int)>;
using EditorSpawnImpl      = std::function<entt::entity(entt::registry&, const std::string&, Vector2, Vector3)>;
using EditorRaycastImpl    = std::function<entt::entity(entt::registry&, Ray, int)>;
using EditorActiveModeImpl = std::function<int(entt::registry&)>;
using EditorIsActiveImpl   = std::function<bool(entt::registry&)>;
void register_editor_hit_test_impl(EditorHitTestImpl fn) noexcept;
void register_editor_spawn_impl(EditorSpawnImpl fn) noexcept;
void register_editor_raycast_impl(EditorRaycastImpl fn) noexcept;
void register_editor_active_mode_impl(EditorActiveModeImpl fn) noexcept;
void register_editor_is_active_impl(EditorIsActiveImpl fn) noexcept;

// ── Editor camera rig lifecycle ───────────────────────────────────────────────
// Impl callbacks registered from generated_init_project; they reference
// generated component types (Camera, Viewport, WorldTransform, EditorCamera2D/3D).
using EditorCameraEnterImpl      = std::function<entt::entity(entt::registry&, bool)>;
using EditorCameraExitImpl       = std::function<void(entt::registry&, entt::entity)>;
using EditorApplyCamera2DImpl    = std::function<void(entt::registry&, entt::entity, Vector2, float)>;
using EditorApplyCamera3DImpl    = std::function<void(entt::registry&, entt::entity, Vector3, Quat)>;
using EditorEntityPosition2DImpl = std::function<Vector2(entt::registry&, entt::entity)>;
using EditorEntityPosition3DImpl = std::function<Vector3(entt::registry&, entt::entity)>;

void register_editor_camera_enter_impl(EditorCameraEnterImpl fn) noexcept;
void register_editor_camera_exit_impl(EditorCameraExitImpl fn) noexcept;
void register_editor_apply_camera_2d_impl(EditorApplyCamera2DImpl fn) noexcept;
void register_editor_apply_camera_3d_impl(EditorApplyCamera3DImpl fn) noexcept;
void register_editor_entity_position_2d_impl(EditorEntityPosition2DImpl fn) noexcept;
void register_editor_entity_position_3d_impl(EditorEntityPosition3DImpl fn) noexcept;

void set_editor_saved_viewports(std::vector<entt::entity> viewports) noexcept;
[[nodiscard]] const std::vector<entt::entity>& editor_saved_viewports() noexcept;
[[nodiscard]] entt::entity editor_rig_entity() noexcept;

entt::entity editor_camera_enter(entt::registry& registry, bool use_3d) noexcept;
void editor_camera_exit(entt::registry& registry) noexcept;
void editor_apply_camera_2d(entt::registry& registry, Vector2 view_center, float zoom) noexcept;
void editor_apply_camera_3d(entt::registry& registry, Vector3 position, Quat rotation) noexcept;
[[nodiscard]] float editor_wheel_delta() noexcept;
[[nodiscard]] Vector2 editor_mouse_delta_screen() noexcept;
[[nodiscard]] Vector2 editor_entity_position_2d(entt::registry& registry, entt::entity entity_id) noexcept;
[[nodiscard]] Vector3 editor_entity_position_3d(entt::registry& registry, entt::entity entity_id) noexcept;

/// 3D raycast: return the nearest entity hit by the picking ray through
/// screen_pos, or entt::null (always null when no impl is registered).
[[nodiscard]] entt::entity editor_raycast_3d(entt::registry& registry, Vector2 screen_pos, int mask) noexcept;

/// Convert a screen position to a 2D world position.
[[nodiscard]] Vector2 editor_screen_to_world_2d(Vector2 screen) noexcept;

/// Return the current frame's mouse delta in 2D world space.
[[nodiscard]] Vector2 editor_mouse_delta_2d() noexcept;

/// Convert a screen-space delta to a 2D world-space delta (std.camera.flat.screen_delta_to_world).
[[nodiscard]] Vector2 screen_delta_to_world_2d(Vector2 delta) noexcept;

/// Pure ray/plane intersection behind editor_plane_project_3d: nullopt when the
/// ray is parallel to the plane or the intersection lies behind the ray origin.
[[nodiscard]] std::optional<Vector3> editor_ray_plane_intersect(Ray ray,
                                                                Vector3 plane_origin,
                                                                Vector3 plane_normal) noexcept;

/// Project a screen position onto a 3D plane.
[[nodiscard]] Vector3 editor_plane_project_3d(Vector2 screen, Vector3 plane_origin, Vector3 plane_normal) noexcept;

/// Return the current frame's mouse delta in 3D world space.
[[nodiscard]] Vector3 editor_mouse_delta_3d() noexcept;

/// World-space delta for a screen-space cursor movement on a 3D plane
/// (std.camera.volume.screen_delta_on_plane).
[[nodiscard]] Vector3 screen_delta_on_plane_3d(Vector2 screen,
                                               Vector2 delta,
                                               Vector3 plane_origin,
                                               Vector3 plane_normal) noexcept;

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent);

void destroy_entity_recursive(
    entt::registry& registry,
    entt::entity entity,
    const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children);

// Consume a declared button for the rest of the frame. Inline (like the query
// adapters below) because it resolves through the generated
// cactus_input_button_key/_mouse tables, which only exist in generated code.
inline void consume_input_button(std::uint8_t button) noexcept {
    const int mouse_button = cactus_input_button_mouse(button);
    if (mouse_button >= 0) {
        mark_input_mouse_consumed(mouse_button);
        return;
    }
    mark_input_key_consumed(cactus_input_button_key(button));
}

// editor_consume is inline for the same reason: resolves through generated tables.
inline void editor_consume(std::uint8_t button) noexcept {
    consume_input_button(button);
}

[[nodiscard]] inline bool pressed(std::uint8_t button) noexcept {
    const int mouse_button = cactus_input_button_mouse(button);
    if (mouse_button >= 0) {
        return !is_input_mouse_consumed(mouse_button) && cactus::runtime::raylib::IsMouseButtonPressed(mouse_button);
    }
    const int key = cactus_input_button_key(button);
    return key != 0 && !is_input_key_consumed(key) && cactus::runtime::raylib::IsKeyPressed(key);
}

[[nodiscard]] inline bool down(std::uint8_t button) noexcept {
    const int mouse_button = cactus_input_button_mouse(button);
    if (mouse_button >= 0) {
        return !is_input_mouse_consumed(mouse_button) && cactus::runtime::raylib::IsMouseButtonDown(mouse_button);
    }
    const int key = cactus_input_button_key(button);
    return key != 0 && !is_input_key_consumed(key) && cactus::runtime::raylib::IsKeyDown(key);
}

[[nodiscard]] inline bool released(std::uint8_t button) noexcept {
    const int mouse_button = cactus_input_button_mouse(button);
    if (mouse_button >= 0) {
        return !is_input_mouse_consumed(mouse_button) && cactus::runtime::raylib::IsMouseButtonReleased(mouse_button);
    }
    const int key = cactus_input_button_key(button);
    return key != 0 && !is_input_key_consumed(key) && cactus::runtime::raylib::IsKeyReleased(key);
}

[[nodiscard]] inline float axis(std::uint8_t action) noexcept {
    return cactus_input_axis_value(action);
}

[[nodiscard]] inline Vector2 axis2(std::uint8_t x_action, std::uint8_t y_action) noexcept {
    return Vector2{.x = axis(x_action), .y = axis(y_action)};
}

[[nodiscard]] inline Vector2 mouse_position() noexcept {
    return cactus::runtime::raylib::GetMousePosition();
}

void generated_setup_dispatcher(entt::dispatcher& dispatcher);
void generated_init_project(entt::registry& registry);
void generated_load_project(entt::registry& registry);
void generated_update_project(entt::registry& registry, entt::dispatcher& dispatcher, float dt);
void generated_render_project(entt::registry& registry, entt::dispatcher& dispatcher);
[[nodiscard]] ProjectConfig generated_project_config() noexcept;

}  // namespace cactus::runtime::entt_backend