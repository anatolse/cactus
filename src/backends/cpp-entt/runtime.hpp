#pragma once

#include "common/cactus_runtime.hpp"
#include "common/persistence_document.hpp"
#include "common/persistence_validation.hpp"

#include "backends/cpp-entt/raylib_io.hpp"
#include "backends/cpp-entt/spatial_query.hpp"

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cactus::runtime::entt_backend {

int cactus_input_button_key(std::uint8_t button) noexcept;
int cactus_input_button_mouse(std::uint8_t button) noexcept;
float cactus_input_axis_value(std::uint8_t action) noexcept;
void set_cursor_captured(bool captured) noexcept;

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

// ── Render-pass rendering (dsl-render-passes Quads pass kind) ──────────────────
// Generic, program-independent shader load/cache plus the fixed 6-vertex quad
// submission every Quads render-pass phase issues once per matching instance;
// GLSL source text, uniform names/values, and the per-instance filter loop are
// all program-specific and therefore generated, not part of this runtime.
struct RenderPassShaderState {
    Shader shader{};
    bool loaded{false};
    bool attempted{false};
};

// Compiles and caches `vertex_src`/`fragment_src` into `state` on first call;
// returns the cached shader on every call once loaded, or nullptr if loading
// was never attempted successfully (no real GL context, or a shader compile
// error) — callers treat nullptr as "skip this render-pass phase's draw step
// this frame," mirroring the lighting shader's ensure/nullptr contract.
[[nodiscard]] Shader* ensure_render_pass_shader(RenderPassShaderState& state,
                                                const char* vertex_src,
                                                const char* fragment_src);

// Submits one Quads instance's fixed 6-vertex quad (2 triangles) to whichever
// shader is currently bound via BeginShaderMode. The generated render-pass
// vertex shader derives `corner`/`uv` from gl_VertexID (Decision 2's fixed
// corner table is baked into the shader itself) and reads per-instance data
// from uniforms the caller sets before this call, so the vertex *positions*
// submitted here are unused placeholders — this call exists only to advance
// gl_VertexID six times per instance.
void draw_render_pass_quad_instance() noexcept;

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
    int active_directional_lights{0};
    int missing_assets{0};
    int submitted_ui_primitives{0};
    bool used_default_2d_camera{false};
    bool used_default_3d_camera{false};
    bool used_lit_mesh_shader{false};
    std::vector<int> drawn_sprite_layers;
    // Resolved (material-placeholder color × Renderer.color tint) for each
    // mesh submission this frame, in submission order — mirrors
    // drawn_sprite_layers as the observable-without-a-window channel for
    // std.render.meshes.Renderer's color tint.
    std::vector<Color> submitted_mesh_colors;
    std::vector<Color> submitted_model_colors;
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
                 bool cast_shadow,
                 Color tint) noexcept;

// ── Mesh placeholder geometry (std.render.meshes.Renderer) ────────────────
// Placeholder mesh assets have no real geometry file backing them (like the
// material color heuristic they sit beside in runtime.cpp); shape is
// inferred from the asset's declared path/id by substring match. Exposed
// standalone (like compute_image_draw_rects below) so this pure selection
// logic is unit-testable without a raylib window.
enum class MeshPlaceholderShape : std::uint8_t { Cube, Sphere };
[[nodiscard]] MeshPlaceholderShape placeholder_mesh_shape(std::string_view asset_id) noexcept;
void submit_model(Vector3 position,
                  Quat rotation,
                  Vector3 scale,
                  AssetHandle model,
                  bool visible,
                  bool cast_shadow,
                  Color tint = WHITE) noexcept;
// Animated variant: carries the entity's ModelAnimator (clip, time) so the
// flush re-poses the shared model per submission (dsl-model-animation).
void submit_model(Vector3 position,
                  Quat rotation,
                  Vector3 scale,
                  AssetHandle model,
                  bool visible,
                  bool cast_shadow,
                  int clip,
                  float time,
                  Color tint = WHITE) noexcept;

// ── Animation introspection extern func bridges (std.render.models) ───────────

/// Number of animation clips in the model; 0 for an unresolvable handle.
/// Triggers the model's lazy load, so it works before the first draw.
[[nodiscard]] int model_animation_count(AssetHandle model) noexcept;

/// Clip name as stored in the model file; "" for a bad handle or index.
[[nodiscard]] std::string model_animation_name(AssetHandle model, int clip) noexcept;

/// Clip duration in seconds (keyframes / glTF sampling rate); 0 for a bad
/// handle or index. The generated ModelAnimation rule wraps time by this.
[[nodiscard]] float model_animation_duration(AssetHandle model, int clip) noexcept;

// ── Opt-in model playback (std.render.models ModelPlayback/AnimationCue) ──────
// The legacy no-ModelPlayback tick path (wrap-and-loop, no events) is untouched
// elsewhere; this is the parallel opt-in surface for one-shot playback,
// authored cue markers, and completion events. Every function here is pure —
// no entt::registry dependency — so time arithmetic and baseline resolution
// are unit-testable without any ECS or generated code involved, the same way
// PointerCandidate's pure sort/blocking rules are below.

/// Per-update bound on requested advancement, in multiples of clip duration
/// (dsl-model-animation-events D4): guards against an unbounded marker/
/// completion burst from an extreme dt or speed value.
constexpr int kMaxPlaybackDurationsPerUpdate = 64;

/// One authored marker cue, already resolved to the values that matter for
/// interval crossing (child entity identity and diagnostic dedup live in
/// AnimationCueCache below, not here).
struct AnimationCueEntry {
    int clip{};
    float time{};
    std::string name;

    friend bool operator==(const AnimationCueEntry&, const AnimationCueEntry&) = default;
};

struct PlaybackAdvanceResult {
    float new_time{};
    bool completed{false};
    bool capped{false};
    std::vector<std::string> crossed_cue_names;
};

/// Advances a clip-relative time by `requested_delta` seconds (dt * speed;
/// negative for reverse playback) within [0, duration). `once` clamps at the
/// endpoint matching the direction of travel instead of wrapping; `completed`
/// reports true only on the update that transitions onto that endpoint, so
/// calling again from an already-clamped `old_time` is idempotent (no
/// duplicate completion). `cues` must already be filtered to the active clip
/// and stable-sorted by time, ties broken by caller-supplied order (child
/// creation order). Non-finite inputs or a non-positive duration are total:
/// `old_time` passes through unchanged and no cue or completion is reported.
[[nodiscard]] PlaybackAdvanceResult advance_model_playback(float old_time,
                                                           float requested_delta,
                                                           float duration,
                                                           bool once,
                                                           std::span<const AnimationCueEntry> cues) noexcept;

/// Per-owner playback bookkeeping the opt-in tick path persists between
/// frames. `prior_clip` starts at -1 so the very first tick for an entity is
/// always treated as an interruption (fresh baseline, latch cleared) without
/// a separate "have we seen this entity before" flag.
struct ModelPlaybackBookkeeping {
    int prior_clip{-1};
    int prior_revision{0};
    float prior_time{0.0F};
    bool completed{false};
    // Sticks until a tick isn't capped, so a sustained extreme dt/speed only
    // produces one diagnostic per streak instead of one every frame.
    bool cap_diagnosed{false};
};

struct PlaybackBaseline {
    float time{};
    bool completed{false};
};

/// Resolves this tick's starting time and completion latch from authored
/// state versus what the rule itself committed last tick: a clip or revision
/// change interrupts (fresh baseline at the authored time, latch cleared); an
/// authored time that no longer matches what was last committed is an
/// explicit seek (adopts the new value without replaying skipped cues,
/// latch untouched); otherwise playback continues from where it left off.
[[nodiscard]] PlaybackBaseline resolve_playback_baseline(int clip,
                                                         int revision,
                                                         float authored_time,
                                                         const ModelPlaybackBookkeeping& prior) noexcept;

/// Records that an entity's requested advancement exceeded
/// kMaxPlaybackDurationsPerUpdate and was clamped. Callers dedup via
/// ModelPlaybackBookkeeping::cap_diagnosed so a sustained extreme dt/speed
/// produces one diagnostic per streak instead of one every frame.
void diagnose_capped_playback_advance(entt::entity entity) noexcept;

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

// Clears saved viewports and the camera rig entity without invoking the
// registered exit-impl callback: world replacement has already discarded
// the registry those handles pointed into, so there is nothing left for
// that callback to act on.
void reset_editor_camera_rig_state() noexcept;

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

// ── Per-entity creation ordering (dsl-pair-relations) ──────────────────────
// Monotonic, non-reused per-entity creation order: pair handlers sort their
// binding snapshots by this ordinal so tuple and emitted-event order is
// deterministic and backend-independent. Assigned once at every entity's
// creation site, never reassigned.
struct CreationOrdinal {
    std::uint64_t value{};
};

[[nodiscard]] std::uint64_t generated_next_creation_ordinal() noexcept;

// ── World-persistence provenance ───────────────────────────────────────────
// A saved record rebuilds an entity's unmarked state from the values evaluated
// once when it was created, so participating creation sites keep a copy of what
// they emplaced. One component per trait, not one per entity, so removing and
// re-adding a trait overwrites only that trait's copy.
template <typename Trait>
struct Construction {
    Trait value{};
};

// Index into the program's generated archetype-node table. Emplaced by exactly
// the creation sites that retain Construction, so an entity never carries one
// without the other.
struct ArchetypeOrigin {
    std::uint32_t node{};
};

// Takes the value directly when the caller already has it (e.g. the value it
// just emplaced), avoiding a redundant sparse-set lookup on every persisted
// creation/structural site.
template <typename Trait>
void retain_construction(entt::registry& registry, entt::entity entity, const Trait& value) {
    registry.emplace_or_replace<Construction<Trait>>(entity, Construction<Trait>{value});
}

template <typename Trait>
void retain_construction(entt::registry& registry, entt::entity entity) {
    retain_construction<Trait>(registry, entity, registry.get<Trait>(entity));
}

template <typename Trait>
void discard_construction(entt::registry& registry, entt::entity entity) {
    registry.remove<Construction<Trait>>(entity);
}

// Capture refuses to write a record it could not rebuild; a partial record is
// worse than a reported failure.
[[nodiscard]] inline bool has_capture_provenance(const entt::registry& registry, entt::entity entity) noexcept {
    return registry.valid(entity) && registry.all_of<ArchetypeOrigin>(entity);
}

// ── Authored-cue cache (std.render.models AnimationCue) ────────────────────────
// Snapshot-compared against the owner's current AnimationCue children each
// tick; rebuilt (re-validated and re-sorted) only when a child was added,
// removed, reordered, or a cue's own fields changed, so a steady-state owner
// pays a cheap equality check instead of a fresh scan/sort/diagnostic pass
// every frame. Not trait-derived — like Construction<Trait> above — so EnTT's
// sparse-set storage drops it automatically when the owner is destroyed.
struct AnimationCueSnapshotEntry {
    entt::entity child{entt::null};
    AnimationCueEntry entry;

    friend bool operator==(const AnimationCueSnapshotEntry&, const AnimationCueSnapshotEntry&) = default;
};

struct AnimationCueCache {
    std::vector<AnimationCueSnapshotEntry> snapshot;
    // Valid entries only, stable-sorted by (clip, time) so every clip's cues
    // are one contiguous, already time-ordered run — see active_cues_for_clip.
    std::vector<AnimationCueEntry> entries;
};

/// Diagnoses a cue whose time doesn't lie strictly inside its own clip's
/// (0, duration) — including an out-of-range clip index, which
/// model_animation_duration already degrades to a 0 duration for. Called only
/// from refresh_animation_cue_cache's rebuild branch, which itself runs only
/// on a snapshot change, giving the required "once per cue revision" dedup
/// for free instead of needing a separate dedup set. Known gap: a sibling cue
/// changing on the same owner also invalidates the snapshot and re-diagnoses
/// this cue even though it didn't change; accepted rather than adding a
/// separate persistent dedup set for what's expected to be author-time,
/// rarely-mutated-at-runtime data.
void diagnose_invalid_animation_cue(entt::entity owner, int clip, float time, const std::string& name) noexcept;

/// Refreshes (or reuses) `owner`'s cached, validated AnimationCue list, sorted
/// by (clip, time). `ParentComponent`/`AnimationCueComponent` are the
/// program's generated component types for std.core.Parent /
/// std.render.models.AnimationCue — this is a shared, program-independent
/// algorithm parameterized over them, the same idiom Construction<Trait>
/// above uses for arbitrary trait types.
template <typename ParentComponent, typename AnimationCueComponent>
const std::vector<AnimationCueEntry>& refresh_animation_cue_cache(entt::registry& registry,
                                                                   entt::entity owner,
                                                                   AssetHandle model,
                                                                   AnimationCueCache& cache) {
    std::vector<std::pair<std::uint64_t, entt::entity>> ordered;
    for (auto child : registry.view<ParentComponent, AnimationCueComponent>()) {
        if (registry.get<ParentComponent>(child).parent == owner) {
            ordered.emplace_back(registry.get<CreationOrdinal>(child).value, child);
        }
    }
    std::ranges::sort(ordered);

    std::vector<AnimationCueSnapshotEntry> snapshot;
    snapshot.reserve(ordered.size());
    for (const auto& [ordinal, child] : ordered) {
        const auto& cue = registry.get<AnimationCueComponent>(child);
        snapshot.push_back(AnimationCueSnapshotEntry{
            .child = child, .entry = AnimationCueEntry{.clip = cue.clip, .time = cue.time, .name = cue.name}});
    }

    if (snapshot == cache.snapshot) {
        return cache.entries;
    }

    cache.entries.clear();
    for (const auto& snap : snapshot) {
        const float duration = model_animation_duration(model, snap.entry.clip);
        if (!(snap.entry.time > 0.0F) || !(snap.entry.time < duration)) {
            diagnose_invalid_animation_cue(owner, snap.entry.clip, snap.entry.time, snap.entry.name);
            continue;
        }
        cache.entries.push_back(snap.entry);
    }
    std::ranges::stable_sort(cache.entries, {}, [](const AnimationCueEntry& e) { return std::pair{e.clip, e.time}; });
    cache.snapshot = std::move(snapshot);
    return cache.entries;
}

/// Zero-copy view of `entries`' contiguous run for `clip` (entries is sorted
/// by (clip, time), so every clip's cues already form one time-ordered run).
[[nodiscard]] inline std::span<const AnimationCueEntry> active_cues_for_clip(
    const std::vector<AnimationCueEntry>& entries, const int clip) noexcept {
    const auto first = std::ranges::lower_bound(entries, clip, {}, &AnimationCueEntry::clip);
    const auto last  = std::ranges::upper_bound(entries, clip, {}, &AnimationCueEntry::clip);
    const std::span<const AnimationCueEntry> all{entries};
    return all.subspan(static_cast<std::size_t>(first - entries.begin()), static_cast<std::size_t>(last - first));
}

/// Result of one opt-in ModelPlayback tick, already translated from the
/// pure playback math into what the generated tick body needs to apply:
/// ModelAnimator field writes and which events to emit. Mirrors the shape
/// compute_pointer_frame_transitions returns for RoutePointer below — codegen
/// assigns fields and emits typed events in a loop, it doesn't make decisions.
struct ModelPlaybackTickResult {
    float new_time{};
    bool new_playing{};
    bool completed{};
    std::vector<std::string> crossed_cue_names;
};

/// Runs one tick of the opt-in ModelPlayback surface for `entity`: resolves
/// this frame's baseline (continuation, seek, or interruption/restart),
/// advances it if playing and not already completed, and refreshes/queries
/// the entity's authored cue cache. All per-entity state (bookkeeping latch,
/// cue cache) is owned by `entity` itself via get_or_emplace, so callers don't
/// need to know these types exist. `ParentComponent`/`AnimationCueComponent`
/// are the program's generated types, as in refresh_animation_cue_cache.
template <typename ParentComponent, typename AnimationCueComponent>
ModelPlaybackTickResult tick_model_playback(entt::registry& registry,
                                            entt::entity entity,
                                            AssetHandle model,
                                            int clip,
                                            int revision,
                                            float authored_time,
                                            float requested_delta,
                                            bool playing,
                                            bool once) {
    auto& state = registry.get_or_emplace<ModelPlaybackBookkeeping>(entity);

    const auto baseline  = resolve_playback_baseline(clip, revision, authored_time, state);
    state.prior_clip     = clip;
    state.prior_revision = revision;
    state.completed      = baseline.completed;

    ModelPlaybackTickResult result;
    result.new_time    = baseline.time;
    result.new_playing = playing;
    if (!playing || state.completed) {
        state.prior_time = baseline.time;
        return result;
    }

    const float duration = model_animation_duration(model, clip);
    if (!(duration > 0.0F)) {
        state.prior_time = baseline.time;
        return result;
    }

    auto& cache             = registry.get_or_emplace<AnimationCueCache>(entity);
    const auto& all_cues    = refresh_animation_cue_cache<ParentComponent, AnimationCueComponent>(registry, entity, model, cache);
    const auto active_cues  = active_cues_for_clip(all_cues, clip);
    const auto advance      = advance_model_playback(baseline.time, requested_delta, duration, once, active_cues);

    state.prior_time         = advance.new_time;
    result.new_time          = advance.new_time;
    result.crossed_cue_names = advance.crossed_cue_names;
    if (advance.completed) {
        state.completed    = true;
        result.completed   = true;
        result.new_playing = false;
    }
    if (advance.capped && !state.cap_diagnosed) {
        state.cap_diagnosed = true;
        diagnose_capped_playback_advance(entity);
    } else if (!advance.capped) {
        state.cap_diagnosed = false;
    }
    return result;
}

// Hands out one document-local identity per referenced entity. Excluded targets
// get an identity too, so repeated references to the same excluded entity stay
// equal to each other and distinct from a reference to a different one, without
// pulling either into the document.
class DocumentIdMap {
public:
    // Capture knows the eligible-entity count before assigning any identity;
    // reserving avoids repeated rehashing while filling the table.
    void reserve(std::size_t entity_count) { entries_.reserve(entity_count); }

    // Idempotent with respect to a prior reference(): a forward reference made
    // before an entity's own inclusion must keep pointing at the same identity
    // once that entity is included, not silently rebind to a fresh one.
    persistence::DocumentId include(entt::entity entity) {
        auto& entry     = entry_for(entity);
        entry.included = true;
        return entry.id;
    }

    [[nodiscard]] persistence::EntityRef reference(entt::entity entity) {
        if (entity == entt::null) {
            return {};
        }
        const auto& entry = entry_for(entity);
        return persistence::EntityRef{.id = entry.id, .present = entry.included};
    }

    // Guards nested struct capture against the configured depth bound: false
    // means the bound was already reached, so the caller must not recurse and
    // should record an absent value instead, without calling exit_nesting()
    // (nothing was entered). A true result must be paired with exactly one
    // exit_nesting() call once the guarded recursion returns.
    [[nodiscard]] bool enter_nesting() noexcept {
        if (depth_ >= persistence::kMaxPersistenceNestingDepth) {
            exceeded_ = true;
            return false;
        }
        ++depth_;
        return true;
    }

    void exit_nesting() noexcept { --depth_; }

    // A list past the configured collection bound stops growing rather than
    // consuming unbounded memory; the caller marks the document truncated
    // instead of recording a false impression of a complete collection.
    void mark_limit_exceeded() noexcept { exceeded_ = true; }

    [[nodiscard]] bool limit_exceeded() const noexcept { return exceeded_; }

private:
    struct Entry {
        persistence::DocumentId id = 0;
        bool included              = false;
    };

    // One hash probe per call instead of two lookups across two separately
    // hashed containers keyed by the same entity.
    Entry& entry_for(entt::entity entity) {
        auto [it, inserted] = entries_.try_emplace(entity);
        if (inserted) {
            it->second.id = next_++;
        }
        return it->second;
    }

    std::unordered_map<entt::entity, Entry> entries_;
    std::size_t depth_  = 0;
    bool exceeded_      = false;
    persistence::DocumentId next_ = 1;
};

// Resolves document-local identities to the handles restore allocated for
// them in the staged registry — the read-side counterpart to DocumentIdMap.
// Populated by an allocate-all-records pass before any reference is decoded,
// so forward references and cycles among included records need no ordering.
// An absent reference (persistence::EntityRef::present == false) and a
// reference naming an id this map never allocated both resolve to entt::null:
// a stale value on which every total entity_id operation is already a safe
// no-op, so restore need not fabricate distinct stale handles per excluded
// original target the way capture's DocumentIdMap does.
class RestoreIdMap {
public:
    void allocate(persistence::DocumentId id, entt::entity entity) { entities_[id] = entity; }

    [[nodiscard]] entt::entity resolve(const persistence::EntityRef& ref) const {
        if (!ref.present) {
            return entt::null;
        }
        const auto found = entities_.find(ref.id);
        return found == entities_.end() ? entt::entity{entt::null} : found->second;
    }

    // Records that an AssetRef/InputRef field named a declaration this build
    // does not have. Resource resolution can fail deep inside nested struct/
    // list decode calls, so the failure is accumulated here — checked once
    // after reconstruction and before publication — rather than threaded
    // back through every decode function's return type. Only the first
    // failure is kept: one resource-preparation failure is enough to reject
    // the whole document.
    void mark_resource_unresolved(std::string name) {
        if (!unresolved_resource_.has_value()) {
            unresolved_resource_ = std::move(name);
        }
    }

    [[nodiscard]] bool resource_preparation_failed() const noexcept { return unresolved_resource_.has_value(); }
    [[nodiscard]] const std::string& unresolved_resource_name() const noexcept { return *unresolved_resource_; }

private:
    std::unordered_map<persistence::DocumentId, entt::entity> entities_;
    std::optional<std::string> unresolved_resource_;
};

// ── Field value decoding ────────────────────────────────────────────────────
// The inverse of "Field value conversion" above: generated restore calls
// these to turn a decoded persistence::Value back into the backend's native
// representation. A caller must already know the field's kind matches (task
// 4's validation gate is what guarantees that before any of this runs); a
// mismatched variant access is a programming error, not a runtime condition.

[[nodiscard]] inline bool generated_decode_bool(const persistence::Value& value) { return value.as_bool(); }
[[nodiscard]] inline int generated_decode_int(const persistence::Value& value) { return value.as_int(); }
[[nodiscard]] inline float generated_decode_float(const persistence::Value& value) { return value.as_float(); }
[[nodiscard]] inline std::string generated_decode_string(const persistence::Value& value) {
    return value.as_string();
}

[[nodiscard]] inline Vector2 generated_decode_vector2(const persistence::Value& value) {
    const auto& vector = value.as_vector();
    return Vector2{.x = vector.components[0], .y = vector.components[1]};
}

[[nodiscard]] inline Vector3 generated_decode_vector3(const persistence::Value& value) {
    const auto& vector = value.as_vector();
    return Vector3{.x = vector.components[0], .y = vector.components[1], .z = vector.components[2]};
}

[[nodiscard]] inline Quat generated_decode_quat(const persistence::Value& value) {
    const auto& vector = value.as_vector();
    return Quat{.x = vector.components[0], .y = vector.components[1], .z = vector.components[2],
                .w = vector.components[3]};
}

[[nodiscard]] inline Color generated_decode_color(const persistence::Value& value) {
    const auto& vector = value.as_vector();
    return Color{.r = static_cast<unsigned char>(vector.components[0]),
                 .g = static_cast<unsigned char>(vector.components[1]),
                 .b = static_cast<unsigned char>(vector.components[2]),
                 .a = static_cast<unsigned char>(vector.components[3])};
}

[[nodiscard]] inline entt::entity generated_decode_entity(const persistence::Value& value, const RestoreIdMap& ids) {
    return ids.resolve(value.as_entity());
}

// ── Field value conversion ─────────────────────────────────────────────────
// Program-independent, so generated capture calls these instead of restating
// each backend representation inline.

// The schema descriptor (common/persistence_metadata.cpp) reports Int and
// Float fields as exactly 32 bits: the backend's actual storage width, which
// these assertions pin down rather than assume. dsl-type-system describes
// `float` as 64-bit; the backend has always stored C++ `float` (32-bit) for
// every DSL float field, so the schema reports what is actually written, not
// the language's promised width. Closing that language/backend gap is a
// separate, tracked type-system issue — this schema exposes the backend's
// exact representation without redefining language arithmetic.
static_assert(sizeof(int) == 4, "PersistenceValueKind::Int assumes a 32-bit backend int");
static_assert(sizeof(float) == 4, "PersistenceValueKind::Float assumes a 32-bit backend float");

[[nodiscard]] inline persistence::Value to_persistence_value(bool value) {
    return persistence::Value::of_bool(value);
}

[[nodiscard]] inline persistence::Value to_persistence_value(int value) {
    return persistence::Value::of_int(value);
}

[[nodiscard]] inline persistence::Value to_persistence_value(float value) {
    return persistence::Value::of_float(value);
}

[[nodiscard]] inline persistence::Value to_persistence_value(const std::string& value) {
    return persistence::Value::of_string(value);
}

[[nodiscard]] inline persistence::Value to_persistence_value(Vector2 value) {
    return persistence::Value::of_vector(PersistenceValueKind::Float, 2, {value.x, value.y, 0.0F, 0.0F});
}

[[nodiscard]] inline persistence::Value to_persistence_value(Vector3 value) {
    return persistence::Value::of_vector(PersistenceValueKind::Float, 3, {value.x, value.y, value.z, 0.0F});
}

[[nodiscard]] inline persistence::Value to_persistence_value(Quat value) {
    return persistence::Value::of_vector(PersistenceValueKind::Float, 4, {value.x, value.y, value.z, value.w});
}

// Color lanes are bytes in the backend; float carries every one of them exactly.
static_assert(sizeof(Color::r) == 1, "PersistenceValueKind::Int color lanes assume an 8-bit backend channel");
[[nodiscard]] inline persistence::Value to_persistence_value(Color value) {
    return persistence::Value::of_vector(PersistenceValueKind::Int,
                                         4,
                                         {static_cast<float>(value.r),
                                          static_cast<float>(value.g),
                                          static_cast<float>(value.b),
                                          static_cast<float>(value.a)});
}

// ── World-persistence request scheduling ───────────────────────────────────
// SaveRequested occurrences are collected here rather than dispatched to
// gameplay handlers — a runtime-owned effect domain (dsl-stdlib-persistence
// decision 6), the same shape as std.debug's renderers. Requests accumulate
// during one activation and are frozen into a batch only at that
// activation's boundary, so ordering and "later boundary" deferral are
// properties of this queue, not of any one caller.

// Save and restore share one ordered queue (dsl-stdlib-persistence "Save and
// restore share a batch"): a save then a restore accepted at the same
// boundary must process in that emission order, not all-saves-then-all-
// restores, so the save observes the pre-restore world.
enum class PersistenceRequestKind : std::uint8_t { Save, Restore };

struct PendingPersistenceRequest {
    PersistenceRequestKind kind = PersistenceRequestKind::Save;
    std::string slot;
    int request_id = 0;

    friend bool operator==(const PendingPersistenceRequest&, const PendingPersistenceRequest&) = default;
};

// The result of processing one request, whether a save or a restore.
struct PersistenceOutcome {
    bool ok = false;
    std::string slot;
    int request_id = 0;
    std::string code;     // populated when !ok
    std::string message;  // populated when !ok
};

// ── Storage adapter contract (dsl-stdlib-persistence decision 7) ───────────
// Reference for a host writing a custom PersistenceAdapter. An adapter owns
// encoding and storage; it never receives registry access or spawn
// capabilities, only a schema descriptor and an owned typed snapshot. Read
// backs restore (generated_execute_restore_request, persistence_restore_
// emitter.cpp): the adapter only decodes a document, never touches the
// registry — turning it into live entities is generated_restore_world's job.
//
// Error codes: `code`/`message` on PersistenceWriteResult/PersistenceReadResult
// are free-form strings the adapter chooses, surfaced verbatim to gameplay as
// `storage.SaveFailed.code`/`.message` (or the Restore* equivalents). Two
// codes are runtime-owned and never come from an adapter: "adapter_unavailable"
// (generated_execute_save_request/read_persistence_document below, when no
// adapter or that direction's function is registered) and
// "incompatible_schema" (read_persistence_document, when
// persistence::schema_accepts rejects the decoded document — see that
// function for the exact compatibility policy: exact revision+fingerprint
// match, no partial or best-effort compatibility). Restore adds three more
// runtime-owned codes of its own, all from generated_restore_world: "invalid_
// data" (cactus::persistence::validate_document reported a structural problem
// — unknown archetype/trait/field, a dangling or duplicate identity, a
// hierarchy cycle, or a configured size/depth limit), "unsupported_value"
// (validate_document's problems were all value-kind/content problems, e.g. a
// field holds the wrong kind or an out-of-range value, with the document's
// own structure otherwise sound), and "resource_preparation_failure" (the
// document validated and reconstructed, but an AssetRef/InputRef named a
// declaration this build does not have — checked once after allocation,
// before publication, so a preparation failure never mutates the live
// world). An adapter reports everything else about its
// own encoding/storage failure through its own chosen code; "io_failure" is
// the convention the example file adapter below uses and a reasonable
// default for a new adapter's own I/O errors.
//
// Numeric representation limits: the schema and snapshot report the backend's
// actual storage width (see the ABI static_asserts above this section, and
// common/persistence_metadata.cpp), not the DSL's nominal type width. Capture
// truncates rather than fails when a collection or nesting depth exceeds
// common/persistence_document.hpp's kMaxPersistenceCollectionSize/
// kMaxPersistenceNestingDepth — Snapshot::truncated is set instead of the save
// producing an error, since a bound must not turn a large-but-legitimate save
// into a hard failure. A field whose type the schema cannot represent at all
// is rejected at compile time (common/persistence_metadata.cpp's unsupported-
// field diagnostic) and therefore can never reach an adapter as a runtime
// value.
//
// Example encoding and default registration: see persistence_file_adapter.hpp
// for the documented example adapter's on-disk format, and
// register_persistence_adapter below (also generated main()'s default
// registration, cpp_entt_codegen.cpp's emit_backend_main) for how an adapter
// is wired in.

struct PersistenceWriteResult {
    bool ok = false;
    std::string code;     // populated when !ok
    std::string message;  // populated when !ok
};

struct PersistenceReadResult {
    bool ok = false;
    persistence::Snapshot snapshot;  // populated when ok
    std::string code;                // populated when !ok
    std::string message;             // populated when !ok
};

// The result of generated_restore_world (persistence_restore_emitter.cpp):
// ok on a completed reconstruction, otherwise a standard restore error code
// (see cactus_dsl_spec.md's world-persistence restore section) and message.
struct RestoreOutcome {
    bool ok = false;
    std::string code;     // populated when !ok
    std::string message;  // populated when !ok
};

using PersistenceWriteFn = std::function<PersistenceWriteResult(
    const std::string& slot, const persistence::SchemaDescriptor& schema, const persistence::Snapshot& snapshot)>;
using PersistenceReadFn =
    std::function<PersistenceReadResult(const std::string& slot, const persistence::SchemaDescriptor& schema)>;

// A host registers one of these; encoding and storage are entirely the
// adapter's own choice. Either function left unset behaves as
// adapter_unavailable for that direction.
struct PersistenceAdapter {
    PersistenceWriteFn write;
    PersistenceReadFn read;
};

struct PersistenceRuntimeState {
    std::vector<PendingPersistenceRequest> pending;  // frozen into a batch at the boundary
    PersistenceAdapter adapter;                      // unset: every request fails as adapter_unavailable
    bool processing_batch = false;                   // guards against reentrant boundary processing
};

[[nodiscard]] inline PersistenceRuntimeState& generated_persistence_state() {
    static PersistenceRuntimeState state;
    return state;
}

// Resets processing_batch on every exit path, including one an adapter's own
// exception unwinds through, so a single non-compliant custom write/read
// (the ABI cannot enforce "never throws" at compile time) cannot leave the
// reentrancy guard stuck true and silently wedge every future save request.
class ScopedPersistenceBatch {
public:
    explicit ScopedPersistenceBatch(PersistenceRuntimeState& state) noexcept
        : state_(state) {
        state_.processing_batch = true;
    }
    ~ScopedPersistenceBatch() { state_.processing_batch = false; }

    ScopedPersistenceBatch(const ScopedPersistenceBatch&)            = delete;
    ScopedPersistenceBatch& operator=(const ScopedPersistenceBatch&) = delete;
    ScopedPersistenceBatch(ScopedPersistenceBatch&&)                 = delete;
    ScopedPersistenceBatch& operator=(ScopedPersistenceBatch&&)      = delete;

private:
    PersistenceRuntimeState& state_;
};

// The public registration surface (dsl-stdlib-persistence "Saving works
// without host C++ integration"): generated main() registers the example
// file adapter by default (task 5.4); a host building with
// CACTUS_GENERATED_NO_MAIN registers whatever adapter it wants, or clears the
// slot to deliberately keep every request failing as adapter_unavailable.
inline void register_persistence_adapter(PersistenceAdapter adapter) {
    generated_persistence_state().adapter = std::move(adapter);
}

inline void clear_persistence_adapter() {
    generated_persistence_state().adapter = PersistenceAdapter{};
}

inline void generated_queue_save_request(std::string slot, int request_id) {
    generated_persistence_state().pending.push_back(PendingPersistenceRequest{
        .kind = PersistenceRequestKind::Save, .slot = std::move(slot), .request_id = request_id});
}

inline void generated_queue_restore_request(std::string slot, int request_id) {
    generated_persistence_state().pending.push_back(PendingPersistenceRequest{
        .kind = PersistenceRequestKind::Restore, .slot = std::move(slot), .request_id = request_id});
}

// Extracts the current queue as one ordered batch and clears it, so requests
// queued while this batch's outcomes are being delivered join a later batch
// instead of the one currently frozen.
[[nodiscard]] inline std::vector<PendingPersistenceRequest> generated_freeze_persistence_request_batch() {
    std::vector<PendingPersistenceRequest> batch;
    std::swap(batch, generated_persistence_state().pending);
    return batch;
}

[[nodiscard]] inline PersistenceOutcome generated_execute_save_request(const std::string& slot,
                                                                       int request_id,
                                                                       const persistence::SchemaDescriptor& schema,
                                                                       const persistence::Snapshot& snapshot) {
    const auto& write = generated_persistence_state().adapter.write;
    if (!write) {
        return PersistenceOutcome{.ok         = false,
                                  .slot       = slot,
                                  .request_id = request_id,
                                  .code       = "adapter_unavailable",
                                  .message    = "no persistence adapter is registered"};
    }
    const auto result = write(slot, schema, snapshot);
    if (result.ok) {
        return PersistenceOutcome{.ok = true, .slot = slot, .request_id = request_id};
    }
    return PersistenceOutcome{
        .ok = false, .slot = slot, .request_id = request_id, .code = result.code, .message = result.message};
}

// Reads one document back through the registered adapter, rejecting it if
// its schema does not match the running program's — document validation
// stays runtime-owned even when an adapter also validates its own encoding
// (dsl-stdlib-persistence "Storage adapters own format and location").
[[nodiscard]] inline PersistenceReadResult read_persistence_document(const std::string& slot,
                                                                     const persistence::SchemaDescriptor& schema) {
    const auto& read = generated_persistence_state().adapter.read;
    if (!read) {
        return PersistenceReadResult{
            .ok = false, .code = "adapter_unavailable", .message = "no persistence adapter is registered"};
    }
    auto result = read(slot, schema);
    if (!result.ok) {
        return result;
    }
    if (!persistence::schema_accepts(schema, result.snapshot)) {
        return PersistenceReadResult{.ok      = false,
                                     .code    = "incompatible_schema",
                                     .message = "stored document's schema does not match the running program"};
    }
    return result;
}

// ── Activation/event-scheduler machinery (extract-codegen-runtime-scaffolding)
// Structural command queued during an activation (entity spawn/destroy,
// trait add/remove) and applied once the activation commits. Has no
// dependency on the program's EventOccurrence type, so it stays a plain,
// non-template struct.
struct StructuralCommand {
    enum class Kind : std::uint8_t { Spawn, Destroy, Add, Remove };
    Kind kind{};
    std::function<void(entt::registry&)> apply;
};

// Cascade-depth cap shared by emit_event/emit_targeted_event/drain_event_cascade
// below; a queued event whose next depth would exceed this is deferred to the
// next external-event drain instead of being processed within the current one.
inline constexpr std::size_t kMaxEventCascadeDepth = 64;

template <typename T>
inline constexpr bool is_event_occurrence_variant_v = false;
template <typename... Alternatives>
inline constexpr bool is_event_occurrence_variant_v<std::variant<Alternatives...>> = true;

// One event queued during an activation. `Occurrence` is the program's
// generated `EventOccurrence` variant alias (every concrete event type the
// program declares); `occurrence` itself stores whichever alternative was
// emitted, implicitly converted into the variant at construction.
template <typename Occurrence>
struct QueuedEvent {
    Occurrence occurrence;
    std::size_t cascade_depth{};
    std::optional<entt::entity> target;
};

// Placeholder hook type for commit_activation's optional OnSpawn/OnDestroy
// notification parameters (see below) — never invoked, so a handler-less
// program instantiates commit_activation with no spawn/destroy branch at
// all, not just a dynamically-untaken one.
struct NoNotify {};

// Per-activation scheduler state: queued/deferred events, pending structural
// commands, current cascade depth, and the deferred-entity reservation
// cursor. `Occurrence` is the program's `EventOccurrence` variant; codegen
// declares `SchedulerState : ActivationRuntime<EventOccurrence>` (plus its
// own per-phase fields) rather than re-declaring this struct's body inline
// per program.
template <typename Occurrence>
struct ActivationRuntime {
    std::deque<QueuedEvent<Occurrence>> root_event_queue;
    std::deque<QueuedEvent<Occurrence>> event_queue;
    std::deque<QueuedEvent<Occurrence>> deferred_events;
    std::vector<StructuralCommand> commands;
    std::size_t current_cascade_depth{};
    entt::entt_traits<entt::entity>::entity_type next_reserved_entity =
        entt::entt_traits<entt::entity>::entity_mask - 1U;
    bool active{};
};

// Reserves a not-yet-valid entity identifier for a deferred spawn (the real
// entity is created only once its Spawn StructuralCommand applies at
// commit). Counts down from the top of the identifier space so reserved
// identifiers never collide with registry.create()'s bottom-up allocation.
template <typename Occurrence>
[[nodiscard]] entt::entity reserve_entity(entt::registry& registry, ActivationRuntime<Occurrence>& activation) {
    static_assert(is_event_occurrence_variant_v<Occurrence>,
                  "ActivationRuntime<Occurrence>'s Occurrence must be a std::variant<...> of the program's concrete "
                  "event types (the generated EventOccurrence alias)");
    using Traits = entt::entt_traits<entt::entity>;
    auto& next   = activation.next_reserved_entity;
    while (next != 0U) {
        const auto candidate = Traits::construct(next--, 0U);
        if (!registry.valid(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("cactus deferred entity identifier space exhausted");
}

// Queues a structural command (spawn/destroy/add/remove) to apply once the
// current activation commits; throws if called outside an activation, which
// would otherwise silently drop the command.
template <typename Occurrence>
void queue_structural_command(ActivationRuntime<Occurrence>& activation,
                              StructuralCommand::Kind kind,
                              std::function<void(entt::registry&)> apply) {
    if (!activation.active) {
        throw std::runtime_error("cactus structural command queued outside an activation");
    }
    activation.commands.push_back(StructuralCommand{.kind = kind, .apply = std::move(apply)});
}

// Enqueues `occurrence` (any type implicitly convertible to Occurrence, i.e.
// any alternative of the program's EventOccurrence variant) for dispatch
// within the current cascade, or defers it to the next external-event drain
// once kMaxEventCascadeDepth would be exceeded.
template <typename Occurrence, typename ConcreteEvent>
void emit_event(ActivationRuntime<Occurrence>& activation, ConcreteEvent occurrence) {
    const auto next_depth = activation.current_cascade_depth + 1;
    auto queued           = QueuedEvent<Occurrence>{.occurrence = std::move(occurrence), .cascade_depth = next_depth};
    if (next_depth > kMaxEventCascadeDepth) {
        queued.cascade_depth = 0;
        activation.deferred_events.push_back(std::move(queued));
        return;
    }
    activation.event_queue.push_back(std::move(queued));
}

// Targeted counterpart of emit_event: the recipient is evaluated once by the
// caller and carried in the queued envelope so it survives cascade deferral
// unchanged.
template <typename Occurrence, typename ConcreteEvent>
void emit_targeted_event(ActivationRuntime<Occurrence>& activation, ConcreteEvent occurrence, entt::entity target) {
    const auto next_depth = activation.current_cascade_depth + 1;
    auto queued =
        QueuedEvent<Occurrence>{.occurrence = std::move(occurrence), .cascade_depth = next_depth, .target = target};
    if (next_depth > kMaxEventCascadeDepth) {
        queued.cascade_depth = 0;
        activation.deferred_events.push_back(std::move(queued));
        return;
    }
    activation.event_queue.push_back(std::move(queued));
}

// Drains activation.event_queue, dispatching each occurrence through
// `dispatch(registry, concrete_occurrence, target)` via std::visit. A
// targeted occurrence whose recipient is no longer valid is dropped before
// dispatch. `Dispatch` is a template parameter (a codegen-emitted lambda
// wrapping generated_dispatch_event's overload set), not a std::function
// like propagate_hierarchy's callbacks, because this runs once per queued
// event — potentially many times per frame in event-heavy programs — and
// CLAUDE.md ranks generated-code runtime speed over the std::function
// idiom's convenience here; see extract-codegen-runtime-scaffolding
// design.md Decision 4 before "fixing" this back to std::function.
template <typename Occurrence, typename Dispatch>
void drain_event_cascade(ActivationRuntime<Occurrence>& activation, entt::registry& registry, Dispatch dispatch) {
    static_assert(is_event_occurrence_variant_v<Occurrence>,
                  "ActivationRuntime<Occurrence>'s Occurrence must be a std::variant<...> of the program's concrete "
                  "event types (the generated EventOccurrence alias)");
    static_assert(std::is_invocable_v<Dispatch&,
                                      entt::registry&,
                                      const std::variant_alternative_t<0, Occurrence>&,
                                      std::optional<entt::entity>>,
                  "drain_event_cascade's Dispatch must be callable as dispatch(registry, occurrence, target) for "
                  "every EventOccurrence alternative");
    while (!activation.event_queue.empty()) {
        auto queued = std::move(activation.event_queue.front());
        activation.event_queue.pop_front();
        activation.current_cascade_depth = queued.cascade_depth;
        if (queued.target.has_value() && !registry.valid(*queued.target)) {
            continue;
        }
        std::visit([&](const auto& occurrence) { dispatch(registry, occurrence, queued.target); }, queued.occurrence);
    }
    activation.current_cascade_depth = 0;
}

// Calls on_spawn/on_destroy for `command` when the corresponding hook isn't
// NoNotify (extracted out of commit_activation's loop below purely to keep
// that function's cognitive-complexity score under the project's clang-tidy
// threshold; no behavior of its own beyond the two guarded calls).
template <typename Occurrence, typename OnSpawn, typename OnDestroy>
void notify_structural_command(ActivationRuntime<Occurrence>& activation,
                               const StructuralCommand& command,
                               OnSpawn& on_spawn,
                               OnDestroy& on_destroy) {
    if constexpr (!std::is_same_v<std::remove_cvref_t<OnSpawn>, NoNotify>) {
        if (command.kind == StructuralCommand::Kind::Spawn) {
            on_spawn(activation);
        }
    }
    if constexpr (!std::is_same_v<std::remove_cvref_t<OnDestroy>, NoNotify>) {
        if (command.kind == StructuralCommand::Kind::Destroy) {
            on_destroy(activation);
        }
    }
}

// Applies every queued structural command, looping while an OnSpawn/OnDestroy
// hook keeps producing more (bounded by kMaxEventCascadeDepth, same as
// today's inline codegen: a deferred notification queues no new command, so
// the loop terminates). `drain_cascade` is called once per command batch,
// after the whole batch has applied — not per command — so an entire wave of
// structural commands (e.g. every entity in one spawn burst) is fully
// materialized before any spawn/destroy handler observes the registry,
// matching the original inline behavior exactly. It takes a callable rather
// than embedding drain_event_cascade directly so codegen can simply pass
// `&generated_drain_event_cascade` (already forward-declared at the point
// generated_commit_activation is emitted) instead of constructing a Dispatch
// lambda that would need generated_dispatch_event's per-event-type overloads
// visible before they're actually declared later in the same generated file.
// OnSpawn/OnDestroy default to NoNotify so a handler-less program takes the
// single-pass branch below with no notification code instantiated at all.
template <typename Occurrence, typename DrainCascade, typename OnSpawn = NoNotify, typename OnDestroy = NoNotify>
void commit_activation(ActivationRuntime<Occurrence>& activation,
                       entt::registry& registry,
                       DrainCascade drain_cascade,
                       OnSpawn on_spawn     = {},
                       OnDestroy on_destroy = {}) {
    static_assert(is_event_occurrence_variant_v<Occurrence>,
                  "ActivationRuntime<Occurrence>'s Occurrence must be a std::variant<...> of the program's concrete "
                  "event types (the generated EventOccurrence alias)");
    static_assert(std::is_invocable_v<DrainCascade&, entt::registry&>,
                  "commit_activation's DrainCascade must be callable as drain_cascade(registry)");
    constexpr bool has_spawn_hook   = !std::is_same_v<OnSpawn, NoNotify>;
    constexpr bool has_destroy_hook = !std::is_same_v<OnDestroy, NoNotify>;
    if constexpr (!has_spawn_hook && !has_destroy_hook) {
        auto commands = std::move(activation.commands);
        activation.commands.clear();
        for (auto& command : commands) {
            command.apply(registry);
        }
    } else {
        while (!activation.commands.empty()) {
            auto commands = std::move(activation.commands);
            activation.commands.clear();
            for (auto& command : commands) {
                command.apply(registry);
                notify_structural_command(activation, command, on_spawn, on_destroy);
            }
            drain_cascade(registry);
        }
    }
}

// ── Projected-trait tracking (backend-cpp-entt registry-based projected traits)
// One template instantiated per projected component type, replacing the
// per-type remember/project/cancel/clear quartet codegen used to emit as
// inline text. `remember`/`project` guard against a stale/non-live `entity`
// internally (EnTT's registry::valid() is well-defined — "total" — on any
// entity value, including a destroyed or never-created one), so this type is
// safe to exercise directly, not only behind the `if (registry.valid(...))`
// guard codegen's call sites also keep. Tag components (no fields) and
// data-bearing components share the same template, selected via
// `std::is_empty_v<Component>`.
template <typename Component>
class ProjectedTraitTracker {
public:
    // Records the entity's pre-projection state exactly once per frame — a
    // repeated call for an already-tracked entity is a no-op, so a later
    // `clear` restores the value that existed before the *first* projection
    // (backend-cpp-entt "Repeated projection coalesces").
    void remember(entt::registry& registry, entt::entity entity) {
        if (!registry.valid(entity) || previous_.contains(entity)) {
            return;
        }
        entities_.push_back(entity);
        if constexpr (std::is_empty_v<Component>) {
            previous_.emplace(entity, registry.all_of<Component>(entity));
        } else {
            if (const auto* existing = registry.try_get<Component>(entity); existing != nullptr) {
                previous_.emplace(entity, *existing);
            } else {
                previous_.emplace(entity, std::nullopt);
            }
        }
    }

    // Materializes the projected value as a registry component. Tag
    // components return void; data-bearing components return a mutable
    // reference so callers can patch individual fields, mirroring the
    // per-type `project_<T>` quartet codegen used to emit. A stale/non-live
    // `entity` is a safe no-op; the data-bearing branch returns a scratch
    // instance in that case so the call-site field-assignment shape stays
    // uniform without ever touching the registry with an invalid handle.
    decltype(auto) project(entt::registry& registry, entt::entity entity) {
        if constexpr (std::is_empty_v<Component>) {
            if (!registry.valid(entity)) {
                return;
            }
            remember(registry, entity);
            registry.emplace_or_replace<Component>(entity);
        } else {
            if (!registry.valid(entity)) {
                static Component discarded{};
                discarded = Component{};
                return (discarded);  // parenthesized: decltype(auto) must deduce Component&, not Component
            }
            remember(registry, entity);
            if (auto* current = registry.try_get<Component>(entity); current != nullptr) {
                return *current;
            }
            return registry.emplace<Component>(entity);
        }
    }

    // A durable write (AddTrait/RemoveTrait) to the same (entity, trait) now
    // owns the component going forward: forget the projected-cleanup
    // obligation without touching the registry.
    void cancel(entt::entity entity) {
        previous_.erase(entity);
    }

    // True while this entity's component is a frame-local projection rather
    // than durable structure, which world capture must not record.
    [[nodiscard]] bool is_projected(entt::entity entity) const {
        return previous_.contains(entity);
    }

    // Restores or removes every tracked entity's component per the recorded
    // pre-projection snapshot, then resets tracking for the next frame. An
    // entity destroyed after being projected (no longer valid) is skipped,
    // matching total entity_id semantics.
    void clear(entt::registry& registry) {
        for (const auto entity : entities_) {
            const auto previous_it = previous_.find(entity);
            if (previous_it == previous_.end() || !registry.valid(entity)) {
                continue;
            }
            if constexpr (std::is_empty_v<Component>) {
                if (previous_it->second) {
                    registry.emplace_or_replace<Component>(entity);
                } else if (registry.all_of<Component>(entity)) {
                    registry.remove<Component>(entity);
                }
            } else {
                if (previous_it->second.has_value()) {
                    registry.emplace_or_replace<Component>(entity, *previous_it->second);
                } else if (registry.all_of<Component>(entity)) {
                    registry.remove<Component>(entity);
                }
            }
        }
        entities_.clear();
        previous_.clear();
    }

private:
    std::vector<entt::entity> entities_;
    std::unordered_map<entt::entity, std::conditional_t<std::is_empty_v<Component>, bool, std::optional<Component>>>
        previous_;
};

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent);

void destroy_entity_recursive(
    entt::registry& registry,
    entt::entity entity,
    const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children);

// Clears the cascade re-entry guard destroy_entity_recursive keeps across
// calls. Normally empty again by the time any top-level call returns; this
// exists so world replacement can't leave it holding handles into a
// replaced registry if an exception ever unwound through an active cascade.
void reset_pending_destruction_state() noexcept;

// ── Sweep-and-prune broad phase (spatial-broadphase-runtime capability) ────────
// Runtime-owned, program-independent 2D/3D broad phase: proxies are plain
// (entity, creation ordinal, center, radius) values with no dependency on
// generated component types, following the propagate_hierarchy/
// destroy_entity_recursive runtime-owns-the-algorithm precedent above.
// Candidate pairs are conservative (no false negatives), duplicate-free,
// self-pair-free, and deterministic for a fixed proxy set.

struct Proxy2D {
    entt::entity entity{entt::null};
    std::uint64_t ordinal{0};
    Vector2 center{};
    float radius{0.0F};
};

struct Proxy3D {
    entt::entity entity{entt::null};
    std::uint64_t ordinal{0};
    Vector3 center{};
    float radius{0.0F};
};

// Indices into the proxy span passed to the most recent sync(); first < second
// always (unordered, duplicate-free, self-pair-free).
struct SapCandidatePair {
    std::size_t first;
    std::size_t second;
};

// Benchmark-derived default small-domain threshold (tasks.md 6.4): the
// brute-force fallback wins up to N=128 (499us vs 522us) and the swept path
// pulls ahead from N=256 on (1473us vs 2075us), scaling to a 2.8x win by
// N=1024 — see test_runtime_sap_broadphase.cpp's hidden "[benchmark]" case
// for the full brute-force-vs-swept sweep this was measured from. An
// internal, revisable constant, not a spec-guaranteed value; correctness
// never depends on it (spatial-broadphase-runtime design.md).
inline constexpr std::size_t kSapDefaultSmallDomainThreshold = 256;

// Global test-only override for every SapBroadPhase2D/3D instance's
// small-domain threshold (spatial-broadphase-runtime, design.md): generated
// code constructs its broad-phase instance locally with no handle a test
// could reach, so per-instance set_small_domain_threshold_for_testing (below)
// cannot force a strategy through generated code. nullopt (the default)
// leaves every instance's own threshold untouched; production code never
// calls the setter. Not exposed to Cactus source or codegen.
[[nodiscard]] std::optional<std::size_t> sap_small_domain_threshold_override_for_testing() noexcept;
void set_sap_small_domain_threshold_override_for_testing(std::optional<std::size_t> threshold) noexcept;

class SapBroadPhase2D {
public:
    void sync(std::span<const Proxy2D> proxies);
    [[nodiscard]] std::span<const SapCandidatePair> candidate_pairs() const noexcept;

    // Test-only hooks: axis selection and the small-domain threshold are
    // internal, benchmark-tuned implementation details, never exposed to
    // Cactus source or codegen (spatial-broadphase-runtime design.md).
    [[nodiscard]] int primary_axis_for_testing() const noexcept;
    void set_small_domain_threshold_for_testing(std::size_t threshold) noexcept;

private:
    std::vector<SapCandidatePair> candidates_;
    int primary_axis_{0};
    std::size_t small_domain_threshold_{kSapDefaultSmallDomainThreshold};
};

class SapBroadPhase3D {
public:
    void sync(std::span<const Proxy3D> proxies);
    [[nodiscard]] std::span<const SapCandidatePair> candidate_pairs() const noexcept;

    [[nodiscard]] int primary_axis_for_testing() const noexcept;
    void set_small_domain_threshold_for_testing(std::size_t threshold) noexcept;

private:
    std::vector<SapCandidatePair> candidates_;
    int primary_axis_{0};
    std::size_t small_domain_threshold_{kSapDefaultSmallDomainThreshold};
};

// Given a synced broad phase's candidate pairs and the same proxy span,
// synthesizes the self-tuple every live proxy is always paired with
// (SapBroadPhase itself structurally cannot produce one), expands every
// unordered candidate/self-pair into both directed tuples, sorts the result
// into left-binding-major, creation-ordinal order — the same order the
// Cartesian pair-handler loop already produces — and invokes on_tuple once
// per tuple in that order. The recognized spatial predicate itself is not
// re-checked here: SAP's candidates are a conservative (over-inclusive)
// superset, and on_tuple is expected to be the same per-tuple residual
// predicate evaluation and handler body invocation codegen already emits for
// the Cartesian path, which still re-verifies it exactly
// (spatial-broadphase-runtime, dsl-pair-relations).
void sap_execute_pair_tuples(std::span<const Proxy2D> proxies,
                             std::span<const SapCandidatePair> candidates,
                             const std::function<void(entt::entity, entt::entity)>& on_tuple);
void sap_execute_pair_tuples(std::span<const Proxy3D> proxies,
                             std::span<const SapCandidatePair> candidates,
                             const std::function<void(entt::entity, entt::entity)>& on_tuple);

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
