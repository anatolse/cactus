#pragma once

#include "common/cactus_runtime.hpp"
#include "common/collider_physics.hpp"
#include "common/persistence_document.hpp"
#include "common/persistence_validation.hpp"

#include "backends/cpp-entt/binding_index.hpp"
#include "backends/cpp-entt/creation_order.hpp"
#include "backends/cpp-entt/raylib_io.hpp"
#include "backends/cpp-entt/relation_store.hpp"
#include "backends/cpp-entt/spatial_query.hpp"
#include "backends/cpp-entt/voxel_surfaces.hpp"

#include <entt/entt.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cactus::runtime::entt_backend {

int cactus_input_button_key(std::uint8_t button) noexcept;
int cactus_input_button_mouse(std::uint8_t button) noexcept;
float cactus_input_axis_value(std::uint8_t action) noexcept;
void set_cursor_captured(bool captured) noexcept;

// std.app quit state: set by a QuitRequested handler, read by the generated
// main loop or a custom host, and cleared on generated_init_project.
void request_quit() noexcept;
[[nodiscard]] bool quit_requested() noexcept;
void reset_quit_request() noexcept;

// ── Range faults ────────────────────────────────────────────────────────────
// A ranged `let` value made outside its range, or against a missing container,
// makes its command do nothing. The host reads why from this log; game code
// only observes that nothing happened. Native code that creates entities
// directly, bypassing commands, is not checked.
enum class RangeFaultReason : std::uint8_t { OutOfRange, StaleContainer, MissingContainerTrait };

struct RangeFault {
    std::string_view trait;
    std::string_view field;
    std::int32_t value = 0;
    std::int32_t lo    = 0;
    std::int32_t hi    = 0;
    RangeFaultReason reason = RangeFaultReason::OutOfRange;
};

inline constexpr std::size_t RUNTIME_FAULT_LOG_CAPACITY = 64;

// Records the fault, dropping the oldest past capacity, and prints a warning.
void report_range_fault(const RangeFault& fault);
// Reports `fault` when the check failed; true when it passed.
[[nodiscard]] bool passes_range_check(const std::optional<RangeFault>& fault);
[[nodiscard]] const std::deque<RangeFault>& runtime_faults() noexcept;
// Every fault reported since the last clear, including those the log dropped.
[[nodiscard]] std::size_t runtime_fault_count() noexcept;
void clear_runtime_faults() noexcept;
[[nodiscard]] std::string describe_range_fault(const RangeFault& fault);

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
// The ray through a window pixel, projected through the active viewport's rectangle.
[[nodiscard]] Ray screen_to_world_ray(Vector2 screen, const Camera3D& camera) noexcept;

struct CameraBasis {
    Vector3 forward;
    Vector3 right;
    Vector3 up;
};

// Must match std.camera.volume.camera_basis: to_screen relies on it.
[[nodiscard]] CameraBasis camera_basis(Quat rotation) noexcept;
[[nodiscard]] Camera3D camera_3d_from_pose(Vector3 position, Quat rotation, float fov_y) noexcept;

// The pixel rectangle 3D drawing projects into; the whole window while unset.
void set_active_viewport_rect(Rectangle rect) noexcept;
[[nodiscard]] Rectangle active_viewport_rect() noexcept;
template <typename Viewport>
[[nodiscard]] Rectangle viewport_pixel_rect(const Viewport& viewport, const int width, const int height) noexcept {
    const auto w = static_cast<float>(width);
    const auto h = static_cast<float>(height);
    return Rectangle{.x = viewport.x * w, .y = viewport.y * h, .width = viewport.width * w, .height = viewport.height * h};
}
[[nodiscard]] Matrix viewport_projection(const Camera3D& camera, Rectangle rect) noexcept;
// BeginMode3D/EndMode3D, with the GL viewport and aspect of the active viewport rectangle.
void begin_mode_3d(const Camera3D& camera) noexcept;
void end_mode_3d() noexcept;

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
    // Missing or undecodable standalone textures, at most one per asset.
    std::vector<std::string> texture_diagnostics;
    // Texture files read from disk; each asset is read at most once.
    int texture_file_loads{0};
    // Voxel faces and draw calls this frame, and chunk meshes built so far.
    int voxel_faces_drawn{0};
    int voxel_draw_calls{0};
    int voxel_chunk_builds{0};
};

struct ProjectConfig {
    int window_width;
    int window_height;
    const char* window_title;
    int target_fps;
};

[[nodiscard]] RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept;
void reset_render_debug_state() noexcept;
// ── Voxel volumes (std.voxel.volume) ────────────────────────────────────────
// render_voxel_volumes gets the volume's grid from voxel_volume_begin
// when this frame's cells still need observing (null otherwise), notes every
// solid cell of the volume on it with EarlierCell, and calls voxel_volume_end.
// submit_voxel_volume then draws the volume in the current viewport, hiding
// cells above `ceiling`.
[[nodiscard]] VoxelSurfaceGrid* voxel_volume_begin(entt::entity volume, const VoxelVolumeDesc& desc) noexcept;
void voxel_volume_end(entt::entity volume) noexcept;

// Whether the cell entity `key` was created before `other`; consulted only for
// duplicate cells.
struct EarlierCell {
    const entt::registry* registry = nullptr;

    [[nodiscard]] bool operator()(std::uint64_t key, std::uint64_t other) const {
        const auto entity_of = [](std::uint64_t value) {
            return static_cast<entt::entity>(static_cast<entt::id_type>(value));
        };
        return creation_ordinal(*registry, entity_of(key)) < creation_ordinal(*registry, entity_of(other));
    }
};
void submit_voxel_volume(entt::entity volume, std::optional<int> ceiling) noexcept;
// The viewport entity whose camera is rendering, or null outside a viewport.
void set_active_viewport_entity(entt::entity viewport) noexcept;
[[nodiscard]] entt::entity active_viewport_entity() noexcept;

template <typename Volume, typename Renderer, typename Cell, typename Cutaway, typename Solid, auto Valid>
void render_voxel_volumes(entt::registry& registry) {
    for (const auto [volume_entity, volume, renderer] : registry.view<Volume, Renderer>().each()) {
        if (!renderer.visible || volume.issue != Valid) {
            continue;
        }
        const VoxelVolumeDesc desc{.origin    = volume.origin,
                                   .cell_size = volume.cell_size,
                                   .min       = {volume.min_x, volume.min_y, volume.min_z},
                                   .max       = {volume.max_x, volume.max_y, volume.max_z}};
        if (auto* grid = voxel_volume_begin(volume_entity, desc)) {
            const EarlierCell earlier{.registry = &registry};
            // Walks the cells in storage order, which keeps the scan sequential.
            const auto& solid = registry.storage<Solid>();
            for (const auto [cell_entity, cell] : registry.view<Cell>().each()) {
                if (cell.volume == volume_entity && solid.contains(cell_entity)) {
                    grid->note_solid(cell.x, cell.y, cell.z, cell.texture,
                                     static_cast<std::uint64_t>(entt::to_integral(cell_entity)), earlier);
                }
            }
            voxel_volume_end(volume_entity);
        }
        std::optional<int> ceiling;
        const auto viewer = active_viewport_entity();
        if (const auto* cut = registry.valid(viewer) ? registry.try_get<Cutaway>(viewer) : nullptr;
            cut != nullptr && cut->enabled && cut->volume == volume_entity) {
            ceiling = cut->top;
        }
        submit_voxel_volume(volume_entity, ceiling);
    }
}

// Unloads every shared texture, mesh, material and model once. The generated
// entry point calls it after unload and before closing the window.
void release_render_resources() noexcept;
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

// ── Per-entity creation ordering (dsl-pair-relations) ──────────────────────
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

// ── Named entities ───────────────────────────────────────────────────────

// A handler that reaches `slot`'s traits by name runs a pass only while this
// holds; an identity join folds the entity a group names only while it holds.
template <typename... Traits>
[[nodiscard]] bool named_alive(const entt::registry& registry, entt::entity slot) {
    return registry.valid(slot) && registry.all_of<Traits...>(slot);
}

// ── Entity allocation ────────────────────────────────────────────────────

// Owns slot reuse instead of EnTT so no handle value is ever given out twice
// by one registry: a slot whose version would wrap is retired, not reused.
struct EntityAllocator {
    using index_type = entt::entt_traits<entt::entity>::entity_type;
    std::vector<index_type> free_slots;
    index_type next_fresh{};
};

[[nodiscard]] inline EntityAllocator& entity_allocator(entt::registry& registry) {
    auto& context = registry.ctx();
    if (auto* allocator = context.find<EntityAllocator>()) {
        return *allocator;
    }
    return context.emplace<EntityAllocator>();
}

// A reserved handle is owned by its caller and stays not valid until create_reserved.
[[nodiscard]] inline entt::entity reserve_entity(entt::registry& registry) {
    using Traits                  = entt::entt_traits<entt::entity>;
    constexpr auto null_index     = Traits::to_entity(entt::null);
    constexpr auto unused_version = Traits::to_version(entt::tombstone);
    auto& allocator               = entity_allocator(registry);
    if (!allocator.free_slots.empty()) {
        const auto index = allocator.free_slots.back();
        allocator.free_slots.pop_back();
        return Traits::construct(index, registry.current(Traits::construct(index, 0U)));
    }
    // Skip slots that host code created directly through the registry.
    while (allocator.next_fresh != null_index &&
           registry.current(Traits::construct(allocator.next_fresh, 0U)) != unused_version) {
        ++allocator.next_fresh;
    }
    if (allocator.next_fresh == null_index) {
        throw std::runtime_error("cactus entity handle space exhausted");
    }
    return Traits::construct(allocator.next_fresh++, 0U);
}

[[nodiscard]] inline entt::entity create_reserved(entt::registry& registry, entt::entity reserved) {
    return registry.create(reserved);
}

[[nodiscard]] inline entt::entity create_entity(entt::registry& registry) {
    return create_reserved(registry, reserve_entity(registry));
}

inline void release_entity(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) {
        return;
    }
    // destroy returns the slot's next version; 0 means it wrapped.
    if (registry.destroy(entity) != 0U) {
        entity_allocator(registry).free_slots.push_back(entt::entt_traits<entt::entity>::to_entity(entity));
    }
}

// Creation functions take a hint: a spawn reserves a free identifier, while
// module load pre-allocates every named handle so initializers can refer to
// each other, then fills each one in.
[[nodiscard]] inline entt::entity adopt_or_create(entt::registry& registry, entt::entity hint) {
    return registry.valid(hint) ? hint : create_reserved(registry, hint);
}

struct NamedSlotOrigin {
    std::uint32_t node{};
    entt::entity* slot{};
};

// Points each slot at the first live instance of its declaration node, or
// entt::null, in one pass over the origins.
inline void rebind_named_slots(const entt::registry& registry, std::initializer_list<NamedSlotOrigin> slots) {
    for (const auto& entry : slots) {
        *entry.slot = entt::null;
    }
    for (const auto [entity, origin] : registry.view<const ArchetypeOrigin>().each()) {
        const auto* const match = std::ranges::find_if(slots, [&](const NamedSlotOrigin& entry) {
            return entry.node == origin.node && *entry.slot == entt::null;
        });
        if (match != slots.end()) {
            *match->slot = entity;
        }
    }
}

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

// ── Kept traits ───────────────────────────────────────────────────────────────
// A keep rule's commit: each recipient gets the trait (added, or patched in
// place, which fires no trigger), and every other carrier loses it.

template <typename Trait, bool Tracked>
void apply_kept(entt::registry& registry, entt::entity target, const Trait& value) {
    if (!registry.valid(target)) {
        return;
    }
    if (auto* current = registry.try_get<Trait>(target)) {
        *current = value;
        return;
    }
    registry.emplace<Trait>(target, value);
    if constexpr (Tracked) {
        retain_construction<Trait>(registry, target);
    }
}

// All fields at their defaults, so a present trait needs no patch.
template <typename Trait, bool Tracked>
void apply_kept(entt::registry& registry, entt::entity target) {
    if (!registry.valid(target) || registry.all_of<Trait>(target)) {
        return;
    }
    registry.emplace<Trait>(target);
    if constexpr (Tracked) {
        retain_construction<Trait>(registry, target);
    }
}

template <typename Trait, bool Tracked>
void apply_unkept(entt::registry& registry, entt::entity target) {
    if (!registry.valid(target) || !registry.all_of<Trait>(target)) {
        return;
    }
    registry.remove<Trait>(target);
    if constexpr (Tracked) {
        discard_construction<Trait>(registry, target);
    }
}

// The carriers of `Trait` this run doesn't keep, oldest first.
template <typename Trait, typename Kept>
[[nodiscard]] std::vector<entt::entity> leaving_carriers(const entt::registry& registry, const Kept& kept) {
    std::vector<entt::entity> leaving;
    for (const auto entity : registry.view<const Trait>()) {
        if (!kept(entity)) {
            leaving.push_back(entity);
        }
    }
    return creation_ordered(registry, leaving);
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
    // Capture knows the eligible-entity count before assigning any identity.
    void reserve(std::size_t entity_count) { slots_.reserve(entity_count); }

    // Idempotent with respect to a prior reference(): a forward reference made
    // before an entity's own inclusion must keep pointing at the same identity
    // once that entity is included, not silently rebind to a fresh one.
    persistence::DocumentId include(entt::entity entity) {
        auto& entry    = entry_for(entity);
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

    // Values nest one level per struct and per list. A list that cannot nest
    // records no items and marks the document truncated, so validation never
    // meets a value past the configured depth.
    [[nodiscard]] bool can_nest() const noexcept { return depth_ + 1 < persistence::kMaxPersistenceNestingDepth; }
    void enter_nesting() noexcept { ++depth_; }
    void exit_nesting() noexcept { --depth_; }

    // A list past the configured collection bound stops growing rather than
    // consuming unbounded memory; the caller marks the document truncated
    // instead of recording a false impression of a complete collection.
    void mark_limit_exceeded() noexcept { exceeded_ = true; }

    [[nodiscard]] bool limit_exceeded() const noexcept { return exceeded_; }

private:
    struct Entry {
        entt::entity entity        = entt::null;
        persistence::DocumentId id = 0;
        bool included              = false;
    };

    // One slot per entity index. A stale handle whose index a live entity now
    // uses (or the reverse) goes to the overflow map instead.
    Entry& entry_for(entt::entity entity) {
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        if (index >= slots_.size()) {
            slots_.resize(std::max(index + 1, slots_.size() * 2));
        }
        auto& slot = slots_[index];
        if (slot.entity == entity) {
            return slot;
        }
        if (slot.entity == entt::null) {
            slot = Entry{.entity = entity, .id = next_++};
            return slot;
        }
        auto [found, inserted] = overflow_.try_emplace(entity);
        if (inserted) {
            found->second = Entry{.entity = entity, .id = next_++};
        }
        return found->second;
    }

    std::vector<Entry> slots_;
    std::unordered_map<entt::entity, Entry> overflow_;
    std::size_t depth_            = 0;
    bool exceeded_                = false;
    persistence::DocumentId next_ = 1;
};

// Resolves document-local identities to the handles restore allocated for
// them in the staged registry — the read-side counterpart to DocumentIdMap.
// Populated by an allocate-all-records pass, then sealed, before any reference
// is decoded, so forward references and cycles among included records need no
// ordering. An absent reference (persistence::EntityRef::present == false) and a
// reference naming an id this map never allocated both resolve to entt::null:
// a stale value on which every total entity_id operation is already a safe
// no-op, so restore need not fabricate distinct stale handles per excluded
// original target the way capture's DocumentIdMap does.
class RestoreIdMap {
public:
    void reserve(std::size_t count) { entities_.reserve(count); }
    void allocate(persistence::DocumentId id, entt::entity entity) { entities_.emplace_back(id, entity); }
    void seal() { std::ranges::sort(entities_, {}, &std::pair<persistence::DocumentId, entt::entity>::first); }

    [[nodiscard]] entt::entity resolve(const persistence::EntityRef& ref) const {
        if (!ref.present) {
            return entt::null;
        }
        const auto found = std::ranges::lower_bound(entities_, ref.id, {},
                                                    &std::pair<persistence::DocumentId, entt::entity>::first);
        return found == entities_.end() || found->first != ref.id ? entt::entity{entt::null} : found->second;
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
    std::vector<std::pair<persistence::DocumentId, entt::entity>> entities_;
    std::optional<std::string> unresolved_resource_;
};

// ── Typed value encoding ───────────────────────────────────────────────────
// Program-independent pieces of the per-trait encoders and decoders generated
// capture and restore call, so they are not restated in emitted text.

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
// Color lanes are bytes in the backend; float carries every one of them exactly.
static_assert(sizeof(Color::r) == 1, "PersistenceValueKind::Int color lanes assume an 8-bit backend channel");

inline void put_persistence_lanes(persistence::SnapshotBuilder& out, Vector2 value) {
    out.put_lanes(std::array{value.x, value.y});
}

inline void put_persistence_lanes(persistence::SnapshotBuilder& out, Vector3 value) {
    out.put_lanes(std::array{value.x, value.y, value.z});
}

inline void put_persistence_lanes(persistence::SnapshotBuilder& out, Quat value) {
    out.put_lanes(std::array{value.x, value.y, value.z, value.w});
}

inline void put_persistence_lanes(persistence::SnapshotBuilder& out, Color value) {
    out.put_lanes(std::array{static_cast<float>(value.r), static_cast<float>(value.g), static_cast<float>(value.b),
                             static_cast<float>(value.a)});
}

[[nodiscard]] inline Vector2 read_persistence_vector2(persistence::ValueReader& in) {
    std::array<float, 2> lanes{};
    in.get_lanes(lanes);
    return Vector2{.x = lanes[0], .y = lanes[1]};
}

[[nodiscard]] inline Vector3 read_persistence_vector3(persistence::ValueReader& in) {
    std::array<float, 3> lanes{};
    in.get_lanes(lanes);
    return Vector3{.x = lanes[0], .y = lanes[1], .z = lanes[2]};
}

[[nodiscard]] inline Quat read_persistence_quat(persistence::ValueReader& in) {
    std::array<float, 4> lanes{};
    in.get_lanes(lanes);
    return Quat{.x = lanes[0], .y = lanes[1], .z = lanes[2], .w = lanes[3]};
}

[[nodiscard]] inline Color read_persistence_color(persistence::ValueReader& in) {
    std::array<float, 4> lanes{};
    in.get_lanes(lanes);
    return Color{.r = static_cast<unsigned char>(lanes[0]),
                 .g = static_cast<unsigned char>(lanes[1]),
                 .b = static_cast<unsigned char>(lanes[2]),
                 .a = static_cast<unsigned char>(lanes[3])};
}

// The document name for a handle; `fallback` (the empty name) for an unset or
// unknown handle.
template <std::size_t N>
[[nodiscard]] std::uint32_t document_name_at(const std::array<std::uint32_t, N>& names,
                                             std::size_t index,
                                             std::uint32_t fallback) {
    return index < N ? names[index] : fallback;
}

// A count, then each item through `put_item`, within the collection and
// nesting bounds.
template <class List, class PutItem>
void put_persistence_list(persistence::SnapshotBuilder& out, const List& items, DocumentIdMap& ids, PutItem put_item) {
    if (!ids.can_nest()) {
        out.put_index(0);
        ids.mark_limit_exceeded();
        return;
    }
    const auto count = std::min<std::size_t>(items.size(), persistence::kMaxPersistenceCollectionSize);
    if (count < items.size()) {
        ids.mark_limit_exceeded();
    }
    out.put_index(static_cast<std::uint32_t>(count));
    ids.enter_nesting();
    for (std::size_t index = 0; index < count; ++index) {
        put_item(items[index]);
    }
    ids.exit_nesting();
}

template <class Item, class ReadItem>
[[nodiscard]] std::vector<Item> read_persistence_list(persistence::ValueReader& in, ReadItem read_item) {
    const auto count = in.get_index();
    std::vector<Item> items;
    items.reserve(std::min<std::size_t>(count, persistence::kMaxPersistenceCollectionSize));
    for (std::uint32_t index = 0; index < count && !in.failed(); ++index) {
        items.push_back(read_item());
    }
    return items;
}

// A declared asset or input action by name back to this build's handle. The
// empty name is an unset handle; a name this build does not declare marks the
// restore as failing resource preparation.
template <class Handle, class Lookup>
[[nodiscard]] Handle read_persistence_handle(persistence::ValueReader& in, RestoreIdMap& ids, Lookup lookup) {
    const auto name = in.get_name();
    if (name.empty()) {
        return Handle{0};
    }
    const auto handle = lookup(name);
    if (!handle.has_value()) {
        ids.mark_resource_unresolved(std::string(name));
        return Handle{0};
    }
    return *handle;
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

// Restore for a program with nothing persistable: the document is still read
// so adapter and schema errors surface, but there is no world to replace.
[[nodiscard]] inline PersistenceOutcome execute_noop_restore_request(const std::string& slot,
                                                                     int request_id,
                                                                     const persistence::SchemaDescriptor& schema) {
    const auto read = read_persistence_document(slot, schema);
    if (!read.ok) {
        return PersistenceOutcome{
            .ok = false, .slot = slot, .request_id = request_id, .code = read.code, .message = read.message};
    }
    return PersistenceOutcome{.ok = true, .slot = slot, .request_id = request_id};
}

// ── Delivery routes ───────────────────────────────────────────────────────────
// A delivery is delayed exactly when producer and consumer share a delivery
// cycle, so an occurrence's route depends only on its producer's cycle.
inline constexpr std::uint32_t kNoComponent = std::numeric_limits<std::uint32_t>::max();

enum class DeliveryRoute : std::uint8_t { All, ImmediateOnly, DelayedOnly };

[[nodiscard]] constexpr bool route_admits(DeliveryRoute route, std::uint32_t producer, std::uint32_t consumer) {
    switch (route) {
        case DeliveryRoute::All:
            return true;
        case DeliveryRoute::ImmediateOnly:
            return consumer != producer;
        case DeliveryRoute::DelayedOnly:
            return consumer == producer;
    }
    return true;
}

// ── Activation/event-scheduler machinery (extract-codegen-runtime-scaffolding)
// Structural command queued during an activation (entity spawn/destroy,
// trait add/remove) and applied once the activation commits. Has no
// dependency on the program's EventOccurrence type, so it stays a plain,
// non-template struct.
struct StructuralCommand {
    enum class Kind : std::uint8_t { Spawn, Destroy, Add, Remove, Set };
    Kind kind{};
    std::function<void(entt::registry&)> apply;
    std::uint32_t component = kNoComponent;  // the delivery cycle of the handler that queued it
};

// ── Exclusive states ──────────────────────────────────────────────────────────
// A state's slot component holds `index`, the entity's current variant, and a
// `final_mask` of the variants that are never left. Every variant write goes
// through enter_state or leave_state, so the slot and the variant never drift.

// Generated code specializes this once per state.
template <typename Slot>
void remove_state_variant(entt::registry& registry, entt::entity entity, std::size_t index);

template <typename Slot>
[[nodiscard]] bool state_is_final(const Slot& slot) {
    if constexpr (std::is_integral_v<decltype(Slot::final_mask)>) {
        return ((Slot::final_mask >> slot.index) & 1U) != 0U;
    } else {
        return ((Slot::final_mask[slot.index / 64] >> (slot.index % 64)) & 1U) != 0U;
    }
}

template <typename Slot, std::size_t Index, typename Variant>
void enter_state(entt::registry& registry, entt::entity entity, Variant value) {
    const auto* slot = registry.try_get<Slot>(entity);
    if (slot != nullptr && state_is_final(*slot)) {
        return;
    }
    if (slot != nullptr && slot->index != Index) {
        remove_state_variant<Slot>(registry, entity, slot->index);
    }
    if constexpr (std::is_empty_v<Variant>) {
        registry.emplace_or_replace<Variant>(entity);
    } else {
        registry.emplace_or_replace<Variant>(entity, std::move(value));
    }
    registry.emplace_or_replace<Slot>(entity, Slot{.index = Index});
}

template <typename Slot>
void leave_state(entt::registry& registry, entt::entity entity) {
    const auto* slot = registry.try_get<Slot>(entity);
    if (slot == nullptr || state_is_final(*slot)) {
        return;
    }
    remove_state_variant<Slot>(registry, entity, slot->index);
    registry.remove<Slot>(entity);
}

template <typename T>
inline constexpr bool is_event_occurrence_variant_v = false;
template <typename... Alternatives>
inline constexpr bool is_event_occurrence_variant_v<std::variant<Alternatives...>> = true;

// ── Trait lifecycle triggers ──────────────────────────────────────────────
// `on added T` / `on removed T` occurrences, each delivered to the entity
// whose trait set changed. `old` holds T's value just before removal.
template <typename T>
struct TraitAdded {};

template <typename T>
struct TraitRemoved {
    T old;
};

// One event queued during an activation. `Occurrence` is the program's
// generated `EventOccurrence` variant alias (every concrete event type the
// program declares); `occurrence` itself stores whichever alternative was
// emitted, implicitly converted into the variant at construction.
template <typename Occurrence>
struct QueuedEvent {
    Occurrence occurrence;
    std::optional<entt::entity> target;
    DeliveryRoute route     = DeliveryRoute::All;
    std::uint32_t component = kNoComponent;  // the producer's delivery cycle
};

// Occurrences waiting for the next activation of one clock, in production order.
template <typename Occurrence>
using DelayedQueue = std::deque<QueuedEvent<Occurrence>>;

// Per-activation scheduler state: queued events, pending structural commands,
// and the running clock's delayed queue. `Occurrence` is the program's
// `EventOccurrence` variant; codegen declares
// `SchedulerState : ActivationRuntime<EventOccurrence>` (plus its own
// per-phase fields) rather than re-declaring this struct's body inline
// per program.
template <typename Occurrence>
struct ActivationRuntime {
    std::deque<QueuedEvent<Occurrence>> root_event_queue;
    std::deque<QueuedEvent<Occurrence>> event_queue;
    std::vector<StructuralCommand> commands;
    DelayedQueue<Occurrence>* current_delayed = nullptr;
    // The delivery cycle of the handler running now; set only by programs with a cycle.
    std::uint32_t current_component = kNoComponent;
    // Route of the occurrence being dispatched.
    DeliveryRoute delivery_route     = DeliveryRoute::All;
    std::uint32_t delivery_component = kNoComponent;
    bool active{};
};

// Queues a structural command (spawn/destroy/add/remove/set) to apply once the
// current activation commits; throws if called outside an activation, which
// would otherwise silently drop the command.
template <typename Occurrence>
void queue_structural_command(ActivationRuntime<Occurrence>& activation,
                              StructuralCommand::Kind kind,
                              std::function<void(entt::registry&)> apply) {
    if (!activation.active) {
        throw std::runtime_error("cactus structural command queued outside an activation");
    }
    auto& command     = activation.commands.emplace_back(StructuralCommand{.kind = kind, .apply = std::move(apply)});
    command.component = activation.current_component;
}

// Enqueues `occurrence` (any alternative of the program's EventOccurrence
// variant) for dispatch within the current cascade.
template <typename Occurrence, typename ConcreteEvent>
void emit_event(ActivationRuntime<Occurrence>& activation,
                ConcreteEvent occurrence,
                DeliveryRoute route     = DeliveryRoute::All,
                std::uint32_t component = kNoComponent) {
    activation.event_queue.push_back(
        QueuedEvent<Occurrence>{.occurrence = std::move(occurrence), .route = route, .component = component});
}

// Targeted counterpart of emit_event: the recipient is evaluated once by the
// caller and carried in the queued envelope so it survives a delay unchanged.
template <typename Occurrence, typename ConcreteEvent>
void emit_targeted_event(ActivationRuntime<Occurrence>& activation,
                         ConcreteEvent occurrence,
                         entt::entity target,
                         DeliveryRoute route     = DeliveryRoute::All,
                         std::uint32_t component = kNoComponent) {
    activation.event_queue.push_back(QueuedEvent<Occurrence>{
        .occurrence = std::move(occurrence), .target = target, .route = route, .component = component});
}

template <typename Occurrence>
DelayedQueue<Occurrence>& running_clock(ActivationRuntime<Occurrence>& activation) {
    if (activation.current_delayed == nullptr) {
        throw std::runtime_error("cactus delayed occurrence emitted with no activation clock set");
    }
    return *activation.current_delayed;
}

// Queues `occurrence` for the next activation of the running clock.
template <typename Occurrence, typename ConcreteEvent>
void emit_delayed_event(ActivationRuntime<Occurrence>& activation,
                        ConcreteEvent occurrence,
                        DeliveryRoute route,
                        std::uint32_t component) {
    running_clock(activation).push_back(
        QueuedEvent<Occurrence>{.occurrence = std::move(occurrence), .route = route, .component = component});
}

template <typename Occurrence, typename ConcreteEvent>
void emit_delayed_targeted_event(ActivationRuntime<Occurrence>& activation,
                                 ConcreteEvent occurrence,
                                 entt::entity target,
                                 DeliveryRoute route,
                                 std::uint32_t component) {
    running_clock(activation).push_back(QueuedEvent<Occurrence>{
        .occurrence = std::move(occurrence), .target = target, .route = route, .component = component});
}

// Puts the running clock's delayed occurrences at the head of this
// activation's cascade, ahead of what its phase handlers emitted. Delays
// produced from here on wait for the clock's next activation.
template <typename Occurrence>
void deliver_delayed_events(ActivationRuntime<Occurrence>& activation) {
    auto& clock = running_clock(activation);
    activation.event_queue.insert(activation.event_queue.begin(),
                                  std::make_move_iterator(clock.begin()),
                                  std::make_move_iterator(clock.end()));
    clock.clear();
}

// A late lifecycle delivery runs only while the change still holds.
template <typename T>
[[nodiscard]] bool lifecycle_change_holds(const entt::registry& registry,
                                          const TraitAdded<T>& /*occurrence*/,
                                          entt::entity target) {
    return registry.all_of<T>(target);
}

template <typename T>
[[nodiscard]] bool lifecycle_change_holds(const entt::registry& registry,
                                          const TraitRemoved<T>& /*occurrence*/,
                                          entt::entity target) {
    return !registry.all_of<T>(target);
}

template <typename Event>
[[nodiscard]] bool lifecycle_change_holds(const entt::registry& /*registry*/,
                                          const Event& /*occurrence*/,
                                          entt::entity /*target*/) {
    return true;
}

// Whether a queued occurrence still has someone to reach: its recipient is
// alive and, for a delayed lifecycle delivery, the trait change still holds.
template <typename Occurrence>
[[nodiscard]] bool deliverable(const entt::registry& registry, const QueuedEvent<Occurrence>& queued) {
    if (!queued.target.has_value()) {
        return true;
    }
    if (!registry.valid(*queued.target)) {
        return false;
    }
    return queued.route != DeliveryRoute::DelayedOnly ||
           std::visit([&](const auto& occurrence) { return lifecycle_change_holds(registry, occurrence, *queued.target); },
                      queued.occurrence);
}

// Makes `queued`'s route visible to generated dispatch while it runs.
template <typename Occurrence>
void begin_delivery(ActivationRuntime<Occurrence>& activation, const QueuedEvent<Occurrence>& queued) {
    activation.delivery_route     = queued.route;
    activation.delivery_component = queued.component;
}

template <typename Occurrence>
void end_delivery(ActivationRuntime<Occurrence>& activation) {
    activation.delivery_route     = DeliveryRoute::All;
    activation.delivery_component = kNoComponent;
}

// Drains activation.event_queue, dispatching each occurrence through
// `dispatch(registry, concrete_occurrence, target)` via std::visit. An
// occurrence that is no longer deliverable is dropped before dispatch.
// `Dispatch` is a template parameter (a codegen-emitted lambda wrapping
// generated_dispatch_event's overload set), not a std::function like
// propagate_hierarchy's callbacks, because this runs once per queued event —
// potentially many times per frame in event-heavy programs — and CLAUDE.md
// ranks generated-code runtime speed over the std::function idiom's
// convenience here; see extract-codegen-runtime-scaffolding design.md
// Decision 4 before "fixing" this back to std::function.
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
        if (!deliverable(registry, queued)) {
            continue;
        }
        begin_delivery(activation, queued);
        std::visit([&](const auto& occurrence) { dispatch(registry, occurrence, queued.target); }, queued.occurrence);
    }
    end_delivery(activation);
}

// Shared by one program's trackers. Recording is on only while a commit round
// applies commands, so init, restore, and teardown never fire a trigger.
struct LifecycleRound {
    bool recording = false;
    std::uint64_t next_sequence{};
    std::uint64_t command{};  // the command being applied, in queue order
    std::uint32_t component = kNoComponent;  // that command's producer cycle
    bool adds{};  // a replace by this command re-adds the trait
};

// The delivery cycle shared by every cause of a change, or kNoComponent once two differ.
class CommonCycle {
public:
    void fold(std::uint32_t cause) {
        component_ = !seen_ || component_ == cause ? cause : kNoComponent;
        seen_      = true;
    }

    [[nodiscard]] std::uint32_t component() const {
        return component_;
    }

private:
    bool seen_                = false;
    std::uint32_t component_ = kNoComponent;
};

// Which consumers of one lifecycle trigger a producer cycle reaches now and which by delay.
struct LifecycleSplit {
    bool immediate = true;
    bool delayed   = false;
};

// The routes of a program with no delivery cycle: every delivery is immediate.
struct ImmediateLifecycle {
    [[nodiscard]] constexpr LifecycleSplit operator()(std::size_t /*trait_index*/,
                                                      bool /*added*/,
                                                      std::uint32_t /*component*/) const {
        return {};
    }
};

template <typename Occurrence>
struct LifecycleDelivery {
    std::uint64_t command{};
    std::uint64_t sequence{};
    std::uint32_t component = kNoComponent;
    std::size_t trait_index{};
    bool added{};
    Occurrence occurrence;
    entt::entity target{entt::null};
};

// Watches one trait's storage signals and keeps, per entity, the first change
// seen this round: a construct means T was absent before the round, a destroy
// means it was present (EnTT fires on_destroy before removal, so the snapshot
// is still readable there). Every adding and every removing command of the
// round folds its cycle in, so a net change's route does not depend on order.
template <typename T>
class LifecycleTracker {
public:
    void connect(entt::registry& registry, LifecycleRound& round) {
        round_ = &round;
        registry.on_construct<T>().template connect<&LifecycleTracker::on_construct>(*this);
        registry.on_update<T>().template connect<&LifecycleTracker::on_update>(*this);
        registry.on_destroy<T>().template connect<&LifecycleTracker::on_destroy>(*this);
    }

    // Nets this round's changes into targeted occurrences. A destroyed entity
    // is no longer valid, so it fires nothing.
    template <typename Occurrence>
    void collect(entt::registry& registry, std::size_t trait_index, std::vector<LifecycleDelivery<Occurrence>>& out) {
        for (auto& [entity, change] : changes_) {
            const bool valid         = registry.valid(entity);
            const bool present_after = valid && registry.all_of<T>(entity);
            if (!change.present_before && present_after) {
                out.push_back({.command     = change.command,
                               .sequence    = change.sequence,
                               .component   = change.added_by.component(),
                               .trait_index = trait_index,
                               .added       = true,
                               .occurrence  = TraitAdded<T>{},
                               .target      = entity});
            } else if (change.present_before && !present_after && valid) {
                out.push_back({.command     = change.command,
                               .sequence    = change.sequence,
                               .component   = change.removed_by.component(),
                               .trait_index = trait_index,
                               .occurrence  = TraitRemoved<T>{.old = std::move(change.snapshot)},
                               .target      = entity});
            }
        }
        changes_.clear();
    }

private:
    struct Change {
        std::uint64_t command{};
        std::uint64_t sequence{};
        CommonCycle added_by;
        CommonCycle removed_by;
        bool present_before{};
        T snapshot{};
    };

    void on_construct(entt::registry& registry, entt::entity entity) {
        if (round_->recording) {
            touch(registry, entity, false).added_by.fold(round_->component);
        }
    }

    // A replace changes nothing on its own; it only adds a cause to a change already seen.
    void on_update([[maybe_unused]] entt::registry& registry, entt::entity entity) {
        if (!round_->recording || !round_->adds) {
            return;
        }
        if (const auto it = changes_.find(entity); it != changes_.end()) {
            it->second.added_by.fold(round_->component);
        }
    }

    void on_destroy(entt::registry& registry, entt::entity entity) {
        if (round_->recording) {
            touch(registry, entity, true).removed_by.fold(round_->component);
        }
    }

    Change& touch(const entt::registry& registry, entt::entity entity, bool present_before) {
        const auto [it, inserted] = changes_.try_emplace(entity);
        if (!inserted) {
            return it->second;
        }
        it->second.command        = round_->command;
        it->second.sequence       = round_->next_sequence++;
        it->second.present_before = present_before;
        if constexpr (!std::is_empty_v<T>) {
            if (present_before) {
                it->second.snapshot = registry.get<T>(entity);
            }
        }
        return it->second;
    }

    LifecycleRound* round_ = nullptr;
    std::unordered_map<entt::entity, Change> changes_;
};

// The watched-trait set of one program. Codegen instantiates it with exactly
// the traits named by an `on added` / `on removed` trigger. Signals hold a
// pointer to each tracker, so it never moves once connected.
template <typename... Traits>
class LifecycleTrackers {
public:
    LifecycleTrackers()                                    = default;
    ~LifecycleTrackers()                                   = default;
    LifecycleTrackers(const LifecycleTrackers&)            = delete;
    LifecycleTrackers& operator=(const LifecycleTrackers&) = delete;
    LifecycleTrackers(LifecycleTrackers&&)                 = delete;
    LifecycleTrackers& operator=(LifecycleTrackers&&)      = delete;

    void connect(entt::registry& registry) {
        (std::get<LifecycleTracker<Traits>>(trackers_).connect(registry, round_), ...);
    }

    void set_recording(bool recording) {
        round_.recording = recording;
    }

    void begin_command(StructuralCommand::Kind kind, std::uint32_t component) {
        ++round_.command;
        round_.component = component;
        round_.adds      = kind == StructuralCommand::Kind::Add || kind == StructuralCommand::Kind::Spawn;
    }

    // Emits this round's net changes, splitting each by `routes` into the
    // consumers it reaches now and those it reaches in the clock's next
    // activation. Order: the command that first changed them, then the
    // entity's first touch within that command, then removals before
    // additions (a state swap), then tracker (canonical trait) order, so a
    // spawn's traits fire canonically.
    template <typename Occurrence, typename Routes = ImmediateLifecycle>
    void deliver(ActivationRuntime<Occurrence>& activation, entt::registry& registry, const Routes& routes = {}) {
        std::vector<LifecycleDelivery<Occurrence>> deliveries;
        collect_all(registry, deliveries, std::index_sequence_for<Traits...>{});
        std::map<std::pair<std::uint64_t, entt::entity>, std::uint64_t> entity_first_touch;
        for (const auto& delivery : deliveries) {
            auto [it, inserted] =
                entity_first_touch.try_emplace({delivery.command, delivery.target}, delivery.sequence);
            if (!inserted) {
                it->second = std::min(it->second, delivery.sequence);
            }
        }
        std::ranges::sort(deliveries, {}, [&](const LifecycleDelivery<Occurrence>& delivery) {
            return std::tuple{delivery.command,
                              entity_first_touch.at({delivery.command, delivery.target}),
                              delivery.added,
                              delivery.trait_index};
        });
        for (auto& delivery : deliveries) {
            const auto split = routes(delivery.trait_index, delivery.added, delivery.component);
            if (!split.delayed) {
                emit_targeted_event(activation, std::move(delivery.occurrence), delivery.target);
                continue;
            }
            if (split.immediate) {
                emit_targeted_event(activation,
                                    delivery.occurrence,
                                    delivery.target,
                                    DeliveryRoute::ImmediateOnly,
                                    delivery.component);
            }
            emit_delayed_targeted_event(activation,
                                        std::move(delivery.occurrence),
                                        delivery.target,
                                        DeliveryRoute::DelayedOnly,
                                        delivery.component);
        }
    }

    // Startup arrival: `on added T` for every existing entity and every
    // watched T it carries, in creation order, then trait order.
    template <typename Occurrence>
    void deliver_arrivals(ActivationRuntime<Occurrence>& activation, entt::registry& registry) {
        std::vector<std::pair<std::uint64_t, entt::entity>> entities;
        for (const auto [entity, ordinal] : registry.view<CreationOrdinal>().each()) {
            entities.emplace_back(ordinal.value, entity);
        }
        std::ranges::sort(entities);
        for (const auto& [ordinal, entity] : entities) {
            (emit_arrival<Traits>(activation, registry, entity), ...);
        }
    }

private:
    template <typename Occurrence, std::size_t... Indices>
    void collect_all(entt::registry& registry,
                     std::vector<LifecycleDelivery<Occurrence>>& deliveries,
                     std::index_sequence<Indices...> /*trait_indices*/) {
        (std::get<Indices>(trackers_).collect(registry, Indices, deliveries), ...);
    }

    template <typename T, typename Occurrence>
    static void emit_arrival(ActivationRuntime<Occurrence>& activation, entt::registry& registry, entt::entity entity) {
        if (registry.all_of<T>(entity)) {
            emit_targeted_event(activation, TraitAdded<T>{}, entity);
        }
    }

    LifecycleRound round_;
    std::tuple<LifecycleTracker<Traits>...> trackers_;
};

// Applies every queued structural command in one pass. A program with no
// lifecycle trigger calls this overload, so no tracking code is instantiated.
// `drain_cascade` keeps the signature shared with the overload below.
template <typename Occurrence, typename DrainCascade>
void commit_activation(ActivationRuntime<Occurrence>& activation,
                       entt::registry& registry,
                       [[maybe_unused]] DrainCascade drain_cascade) {
    static_assert(is_event_occurrence_variant_v<Occurrence>,
                  "ActivationRuntime<Occurrence>'s Occurrence must be a std::variant<...> of the program's concrete "
                  "event types (the generated EventOccurrence alias)");
    auto commands = std::move(activation.commands);
    activation.commands.clear();
    for (auto& command : commands) {
        command.apply(registry);
    }
}

// With lifecycle trackers, commit runs in rounds: apply the batch, deliver its
// net trait changes, drain the cascade, and repeat while that cascade queued
// more commands. The loop ends because immediate deliveries form no cycle.
// `drain_cascade` is a callable rather than drain_event_cascade itself so
// codegen can pass the forward-declared `&generated_drain_event_cascade`.
template <typename Occurrence, typename DrainCascade, typename Routes = ImmediateLifecycle, typename... Traits>
void commit_activation(ActivationRuntime<Occurrence>& activation,
                       entt::registry& registry,
                       DrainCascade drain_cascade,
                       LifecycleTrackers<Traits...>& lifecycle,
                       const Routes& routes = {}) {
    static_assert(std::is_invocable_v<DrainCascade&, entt::registry&>,
                  "commit_activation's DrainCascade must be callable as drain_cascade(registry)");
    while (!activation.commands.empty()) {
        auto commands = std::move(activation.commands);
        activation.commands.clear();
        lifecycle.set_recording(true);
        for (auto& command : commands) {
            lifecycle.begin_command(command.kind, command.component);
            command.apply(registry);
        }
        lifecycle.set_recording(false);
        lifecycle.deliver(activation, registry, routes);
        drain_cascade(registry);
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
    // Generated code holds trackers as globals; an allocation failure this early terminates anyway.
    ProjectedTraitTracker() noexcept = default;

    // Records the entity's pre-projection state exactly once per frame — a
    // repeated call for an already-tracked entity is a no-op, so a later
    // `clear` restores the value that existed before the *first* projection
    // (backend-cpp-entt "Repeated projection coalesces").
    void remember(entt::registry& registry, entt::entity entity) {
        if (!registry.valid(entity) || find(entity) != nullptr) {
            return;
        }
        slots_.set(entity, static_cast<std::uint32_t>(entries_.size()));
        if constexpr (std::is_empty_v<Component>) {
            entries_.push_back({.entity = entity, .previous = registry.all_of<Component>(entity)});
        } else {
            const auto* existing = registry.try_get<Component>(entity);
            entries_.push_back(
                {.entity = entity, .previous = existing != nullptr ? Previous{*existing} : Previous{std::nullopt}});
        }
    }

    // Materializes the projected value as a registry component. Tag
    // components return void; data-bearing components return a mutable
    // reference so callers can patch individual fields, mirroring the
    // per-type `project_<T>` quartet codegen used to emit. A stale/non-live
    // `entity` is a safe no-op; the data-bearing branch returns a scratch
    // instance in that case so the call-site field-assignment shape stays
    // uniform without ever touching the registry with an invalid handle.
    std::conditional_t<std::is_empty_v<Component>, void, Component&> project(entt::registry& registry,
                                                                             entt::entity entity) {
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
                return discarded;
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
        if (auto* entry = find(entity); entry != nullptr) {
            entry->live = false;
        }
    }

    // True while this entity's component is a frame-local projection rather
    // than durable structure, which world capture must not record.
    [[nodiscard]] bool is_projected(entt::entity entity) const {
        return find(entity) != nullptr;
    }

    // The value a deferred field patch must write: the remembered
    // pre-projection value while projected, so clear() keeps the patch;
    // null when the entity has no durable component.
    [[nodiscard]] Component* durable_value(entt::registry& registry, entt::entity entity)
        requires(!std::is_empty_v<Component>)
    {
        if (!registry.valid(entity)) {
            return nullptr;
        }
        if (auto* entry = find(entity); entry != nullptr) {
            return entry->previous.has_value() ? &*entry->previous : nullptr;
        }
        return registry.try_get<Component>(entity);
    }

    // Restores or removes every tracked entity's component per the recorded
    // pre-projection snapshot, then resets tracking for the next frame. An
    // entity destroyed after being projected (no longer valid) is skipped,
    // matching total entity_id semantics. Storage is kept for the next frame.
    void clear(entt::registry& registry) {
        for (const auto& entry : entries_) {
            if (!entry.live || !registry.valid(entry.entity)) {
                continue;
            }
            if (entry.previous) {
                if constexpr (std::is_empty_v<Component>) {
                    registry.emplace_or_replace<Component>(entry.entity);
                } else {
                    registry.emplace_or_replace<Component>(entry.entity, *entry.previous);
                }
            } else if (registry.all_of<Component>(entry.entity)) {
                registry.remove<Component>(entry.entity);
            }
        }
        entries_.clear();
    }

private:
    using Previous = std::conditional_t<std::is_empty_v<Component>, bool, std::optional<Component>>;

    struct Entry {
        entt::entity entity{entt::null};
        Previous previous{};
        bool live = true;
    };

    // Slots outlive the entries they named; an entry counts only when its handle matches.
    template <typename Self>
    [[nodiscard]] auto* find(this Self& self, entt::entity entity) {
        using EntryPointer = decltype(self.entries_.data());
        const auto slot    = self.slots_.get(entity);
        if (slot >= self.entries_.size()) {
            return EntryPointer{nullptr};
        }
        auto& entry = self.entries_[slot];
        return entry.entity == entity && entry.live ? &entry : EntryPointer{nullptr};
    }

    SparseSlots slots_;
    std::vector<Entry> entries_;
};

// ── Hierarchy (hierarchy-forest) ─────────────────────────────────────────────
// The children of every entity, in creation order, kept current by the
// program's Parent storage signals. Parent must change only through the
// registry API (emplace, emplace_or_replace, replace, patch, remove): a direct
// field write leaves the index stale.
class HierarchyIndex {
public:
    void attach(entt::entity child, entt::entity parent, std::uint64_t ordinal);
    void detach(entt::entity child);
    void clear();

    [[nodiscard]] entt::entity parent_of(entt::entity entity) const;
    [[nodiscard]] entt::entity first_child(entt::entity entity) const;
    [[nodiscard]] entt::entity last_child(entt::entity entity) const;
    [[nodiscard]] entt::entity next_sibling(entt::entity entity) const;
    [[nodiscard]] entt::entity previous_sibling(entt::entity entity) const;
    [[nodiscard]] std::size_t node_count() const noexcept {
        return nodes_.size();
    }

    // Calls `visit` with each Parent carrier, oldest first.
    template <typename Visit>
    void for_each_carrier(Visit&& visit) const {
        for (const auto& [ordinal, entity] : carriers_) {
            const auto* node = find_node(entity);
            if (node != nullptr && node->carrier) {
                visit(entity);
            }
        }
    }

    // Calls `visit` with each child of `entity`, oldest first.
    template <typename Visit>
    void for_each_child(entt::entity entity, Visit&& visit) const {
        for (auto child = first_child(entity); child != entt::null; child = next_sibling(child)) {
            ++child_visits_;
            visit(child);
        }
    }

    // Children yielded by for_each_child so far; tests use it to show no pool scan.
    [[nodiscard]] std::size_t child_visits() const noexcept {
        return child_visits_;
    }

private:
    struct Node {
        entt::entity parent{entt::null};
        entt::entity first_child{entt::null};
        entt::entity last_child{entt::null};
        entt::entity previous{entt::null};
        entt::entity next{entt::null};
        std::uint64_t ordinal{};
        bool carrier = false;
        bool listed  = false;  // has an entry in carriers_
    };

    [[nodiscard]] const Node* find_node(entt::entity entity) const;
    void link(entt::entity child, Node& node, entt::entity parent);
    void unlink(entt::entity child, Node& node);
    void forget_if_unused(entt::entity entity);
    void compact();

    std::unordered_map<entt::entity, Node> nodes_;
    std::vector<std::pair<std::uint64_t, entt::entity>> carriers_;  // sorted; may hold stale entries
    std::size_t stale_carriers_ = 0;
    mutable std::size_t child_visits_ = 0;
};

[[nodiscard]] inline std::uint64_t hierarchy_ordinal(const entt::registry& registry, entt::entity entity) {
    // Entities a host made without an ordinal sort after every generated one.
    static std::uint64_t next_unordered = std::uint64_t{1} << 63U;
    const auto* ordinal = registry.try_get<CreationOrdinal>(entity);
    return ordinal != nullptr ? ordinal->value : next_unordered++;
}

template <typename ParentComponent>
void on_hierarchy_parent_set(entt::registry& registry, entt::entity child) {
    registry.ctx().get<HierarchyIndex>().attach(
        child, registry.get<ParentComponent>(child).parent, hierarchy_ordinal(registry, child));
}

inline void on_hierarchy_parent_removed(entt::registry& registry, entt::entity child) {
    registry.ctx().get<HierarchyIndex>().detach(child);
}

// The registry's index, connected to its ParentComponent storage and built
// from the carriers already there on first use.
template <typename ParentComponent>
HierarchyIndex& hierarchy_index(entt::registry& registry) {
    if (auto* index = registry.ctx().find<HierarchyIndex>(); index != nullptr) {
        return *index;
    }
    auto& index = registry.ctx().emplace<HierarchyIndex>();
    registry.on_construct<ParentComponent>().template connect<&on_hierarchy_parent_set<ParentComponent>>();
    registry.on_update<ParentComponent>().template connect<&on_hierarchy_parent_set<ParentComponent>>();
    registry.on_destroy<ParentComponent>().template connect<&on_hierarchy_parent_removed>();
    std::vector<std::pair<std::uint64_t, entt::entity>> carriers;
    for (const auto [entity, relation] : registry.view<ParentComponent>().each()) {
        carriers.emplace_back(hierarchy_ordinal(registry, entity), entity);
    }
    std::ranges::sort(carriers);
    for (const auto& [ordinal, entity] : carriers) {
        index.attach(entity, registry.get<ParentComponent>(entity).parent, ordinal);
    }
    return index;
}

// True when `parent` is `entity` or one of its descendants, so making it the
// parent would close a cycle. Bounded by the index size, so it ends on any state.
[[nodiscard]] bool reparent_would_cycle(const HierarchyIndex& index, entt::entity entity, entt::entity parent);

// Creation sweep: valid only when every parent is older than its children,
// which holds when the program never reparents. Depth order: a preorder walk
// from the roots, valid for any forest. Both give the same bits.
enum class HierarchyStrategy : std::uint8_t { CreationSweep, DepthOrder };

// Test-only: forces every propagate_hierarchy call to one strategy.
void set_hierarchy_strategy_override(std::optional<HierarchyStrategy> strategy) noexcept;

// An entity with WorldTransform but no LocalTransform is a pose root: it is
// never written, and its children compose onto its current WorldTransform.
struct HierarchyPropagation {
    std::function<bool(entt::entity)> has_local_world;
    std::function<bool(entt::entity)> has_world;
    std::function<void(entt::entity)> copy_local;
    std::function<void(entt::entity, entt::entity)> accumulate_from_parent;
};

void propagate_hierarchy(entt::registry& registry,
                         const HierarchyIndex& index,
                         const HierarchyPropagation& propagation,
                         HierarchyStrategy strategy);

// Derives only the subtrees below the scope roots; scope roots themselves are
// never written.
void propagate_hierarchy_below(const HierarchyIndex& index,
                               const HierarchyPropagation& propagation,
                               std::span<const entt::entity> scope_roots,
                               const std::function<bool(entt::entity)>& is_scope_root);

// Destroys `entity` and its descendants: children before their parent,
// siblings in creation order.
void destroy_entity_recursive(entt::registry& registry, const HierarchyIndex& index, entt::entity entity);

// The children of `of` that `matching` contains, oldest first.
template <typename View>
[[nodiscard]] std::vector<entt::entity> hierarchy_children(const entt::registry& registry,
                                                           const HierarchyIndex& index,
                                                           entt::entity of,
                                                           const View& matching) {
    std::vector<entt::entity> result;
    if (!registry.valid(of)) {
        return result;
    }
    index.for_each_child(of, [&](entt::entity child) {
        if (matching.contains(child)) {
            result.push_back(child);
        }
    });
    return result;
}

// Test-only: makes every HierarchyChildren range iterate a snapshot taken when it begins.
void set_hierarchy_snapshots_forced(bool forced) noexcept;
[[nodiscard]] bool hierarchy_snapshots_forced() noexcept;

// The children of `of` that `matching` contains, oldest first, read from the
// sibling links as iteration advances. It visits what hierarchy_children
// returns only while the hierarchy and `matching` stay unchanged.
template <typename View>
class HierarchyChildren {
public:
    HierarchyChildren(const entt::registry& registry, const HierarchyIndex& index, entt::entity of, View matching)
        : index_(&index), matching_(std::move(matching)) {
        if (!registry.valid(of)) {
            return;
        }
        if (hierarchy_snapshots_forced()) {
            snapshot_ = hierarchy_children(registry, index, of, matching_);
            first_    = snapshot_->empty() ? entt::entity{entt::null} : snapshot_->front();
            return;
        }
        first_ = matching_from(index.first_child(of));
    }

    class Iterator {
    public:
        Iterator(const HierarchyChildren* range, entt::entity current) : range_(range), current_(current) {}

        [[nodiscard]] entt::entity operator*() const noexcept {
            return current_;
        }
        Iterator& operator++() {
            current_ = range_->after(current_, ++position_);
            return *this;
        }
        [[nodiscard]] bool operator==(std::default_sentinel_t /*end*/) const noexcept {
            return current_ == entt::null;
        }

    private:
        const HierarchyChildren* range_;
        entt::entity current_;
        std::size_t position_ = 0;
    };

    [[nodiscard]] Iterator begin() const {
        return Iterator(this, first_);
    }
    [[nodiscard]] static std::default_sentinel_t end() noexcept {
        return std::default_sentinel;
    }

private:
    [[nodiscard]] entt::entity matching_from(entt::entity child) const {
        while (child != entt::null && !matching_.contains(child)) {
            child = index_->next_sibling(child);
        }
        return child;
    }

    [[nodiscard]] entt::entity after(entt::entity current, std::size_t position) const {
        if (snapshot_.has_value()) {
            return position < snapshot_->size() ? (*snapshot_)[position] : entt::entity{entt::null};
        }
        return matching_from(index_->next_sibling(current));
    }

    const HierarchyIndex* index_;
    View matching_;
    entt::entity first_{entt::null};
    std::optional<std::vector<entt::entity>> snapshot_;
};

// Storage one traversal call site reuses across evaluations.
struct HierarchyTraversalBuffer {
    std::vector<entt::entity> order;
    std::vector<entt::entity> stack;
    std::vector<std::pair<std::uint64_t, entt::entity>> ordered;
    std::vector<entt::entity> roots;
    bool leased = false;
};

// Holds a traversal's result in a buffer until it goes out of scope. A
// traversal that finds the buffer leased (a re-entered call site) uses storage
// of its own instead.
class HierarchyTraversal {
public:
    explicit HierarchyTraversal(HierarchyTraversalBuffer& shared) {
        if (shared.leased) {
            owned_ = std::make_unique<HierarchyTraversalBuffer>();
        }
        buffer_         = owned_ != nullptr ? owned_.get() : &shared;
        buffer_->leased = true;
    }
    HierarchyTraversal(const HierarchyTraversal&)            = delete;
    HierarchyTraversal& operator=(const HierarchyTraversal&) = delete;
    HierarchyTraversal(HierarchyTraversal&& other) noexcept
        : buffer_(std::exchange(other.buffer_, nullptr)), owned_(std::move(other.owned_)) {}
    HierarchyTraversal& operator=(HierarchyTraversal&&) = delete;
    ~HierarchyTraversal() {
        if (buffer_ != nullptr) {
            buffer_->leased = false;
        }
    }

    [[nodiscard]] HierarchyTraversalBuffer& buffer() noexcept {
        return *buffer_;
    }
    [[nodiscard]] auto begin() const noexcept {
        return buffer_->order.cbegin();
    }
    [[nodiscard]] auto end() const noexcept {
        return buffer_->order.cend();
    }

private:
    HierarchyTraversalBuffer* buffer_ = nullptr;
    std::unique_ptr<HierarchyTraversalBuffer> owned_;
};

// Fills `roots` with the entities `matching` contains whose parent is missing,
// stale or not matching, oldest first. `ordered` is scratch.
template <typename View>
void fill_hierarchy_roots(const entt::registry& registry,
                          const HierarchyIndex& index,
                          const View& matching,
                          std::vector<std::pair<std::uint64_t, entt::entity>>& ordered,
                          std::vector<entt::entity>& roots) {
    ordered.clear();
    roots.clear();
    for (const auto entity : matching) {
        const auto parent = index.parent_of(entity);
        if (parent == entt::null || !registry.valid(parent) || !matching.contains(parent)) {
            ordered.emplace_back(registry.get<CreationOrdinal>(entity).value, entity);
        }
    }
    std::ranges::sort(ordered);
    for (const auto& [ordinal, entity] : ordered) {
        roots.push_back(entity);
    }
}

// The entities `matching` contains whose parent is missing, stale or not
// matching, oldest first.
template <typename View>
[[nodiscard]] std::vector<entt::entity> hierarchy_roots(const entt::registry& registry,
                                                        const HierarchyIndex& index,
                                                        const View& matching) {
    std::vector<std::pair<std::uint64_t, entt::entity>> ordered;
    std::vector<entt::entity> roots;
    fill_hierarchy_roots(registry, index, matching, ordered, roots);
    return roots;
}

template <typename View>
[[nodiscard]] const std::vector<entt::entity>& hierarchy_roots(const entt::registry& registry,
                                                               const HierarchyIndex& index,
                                                               const View& matching,
                                                               HierarchyTraversalBuffer& buffer) {
    fill_hierarchy_roots(registry, index, matching, buffer.ordered, buffer.roots);
    return buffer.roots;
}

// Fills `buffer.order` with every entity `matching` contains, in hierarchy
// preorder or postorder. An entity whose parent is missing, stale or not
// matching is a root; roots and siblings come oldest first.
template <typename View>
void fill_hierarchy_order(const entt::registry& registry,
                          const HierarchyIndex& index,
                          const View& matching,
                          bool postorder,
                          HierarchyTraversalBuffer& buffer) {
    auto& result = buffer.order;
    auto& stack  = buffer.stack;
    result.clear();
    stack.clear();
    const auto push_matching = [&](entt::entity child) {
        if (matching.contains(child)) {
            stack.push_back(child);
        }
    };
    fill_hierarchy_roots(registry, index, matching, buffer.ordered, buffer.roots);
    for (const auto root : buffer.roots) {
        const auto tree_start = result.size();
        stack.push_back(root);
        while (!stack.empty()) {
            const auto entity = stack.back();
            stack.pop_back();
            result.push_back(entity);
            // Postorder is the reverse of a preorder that takes the youngest child first.
            if (postorder) {
                index.for_each_child(entity, push_matching);
                continue;
            }
            for (auto child = index.last_child(entity); child != entt::null; child = index.previous_sibling(child)) {
                push_matching(child);
            }
        }
        if (postorder) {
            std::reverse(result.begin() + static_cast<std::ptrdiff_t>(tree_start), result.end());
        }
    }
}

template <typename View>
[[nodiscard]] std::vector<entt::entity> hierarchy_order(const entt::registry& registry,
                                                        const HierarchyIndex& index,
                                                        const View& matching,
                                                        bool postorder) {
    HierarchyTraversalBuffer buffer;
    fill_hierarchy_order(registry, index, matching, postorder, buffer);
    return std::move(buffer.order);
}

template <typename View>
[[nodiscard]] HierarchyTraversal hierarchy_order(const entt::registry& registry,
                                                 const HierarchyIndex& index,
                                                 const View& matching,
                                                 bool postorder,
                                                 HierarchyTraversalBuffer& shared) {
    HierarchyTraversal traversal(shared);
    fill_hierarchy_order(registry, index, matching, postorder, traversal.buffer());
    return traversal;
}

// The position of `requested` in a preorder over `matching` whose siblings are
// sorted by `z_of`, then oldest first; 0 when `matching` doesn't contain it.
template <typename View, typename ZIndex>
[[nodiscard]] int hierarchy_stacking_order(const entt::registry& registry,
                                           const HierarchyIndex& index,
                                           const View& matching,
                                           entt::entity requested,
                                           const ZIndex& z_of) {
    int position = 0;
    std::vector<entt::entity> stack;
    std::vector<entt::entity> siblings;
    for (const auto root : hierarchy_roots(registry, index, matching)) {
        stack.push_back(root);
        while (!stack.empty()) {
            const auto entity = stack.back();
            stack.pop_back();
            if (entity == requested) {
                return position;
            }
            ++position;
            siblings.clear();
            index.for_each_child(entity, [&](entt::entity child) {
                if (matching.contains(child)) {
                    siblings.push_back(child);
                }
            });
            std::ranges::stable_sort(siblings, {}, z_of);
            stack.insert(stack.end(), siblings.rbegin(), siblings.rend());
        }
    }
    return 0;
}

// Every node's hierarchy_stacking_order, from one traversal made on the first
// lookup of a pass. A lookup outside a pass computes the order on its own.
class HierarchyPaintOrder {
public:
    // A pass's lookups share one traversal; nothing it reads may change while it lasts.
    class Pass {
    public:
        explicit Pass(HierarchyPaintOrder& order) noexcept : order_(&order) {
            ++order.passes_;
            order.built_ = false;
        }
        Pass(const Pass&)            = delete;
        Pass& operator=(const Pass&) = delete;
        Pass(Pass&&)                 = delete;
        Pass& operator=(Pass&&)      = delete;
        ~Pass() {
            --order_->passes_;
            order_->built_ = false;
        }

    private:
        HierarchyPaintOrder* order_;
    };

    [[nodiscard]] Pass pass() noexcept {
        return Pass(*this);
    }

    template <typename View, typename ZIndex>
    [[nodiscard]] int order_of(const entt::registry& registry,
                               const HierarchyIndex& index,
                               const View& matching,
                               entt::entity requested,
                               const ZIndex& z_of) {
        if (passes_ == 0) {
            return hierarchy_stacking_order(registry, index, matching, requested, z_of);
        }
        if (!built_) {
            build(registry, index, matching, z_of);
        }
        const auto position = positions_.get(requested);
        return position < preorder_.size() && preorder_[position] == requested ? static_cast<int>(position) : 0;
    }

private:
    struct Sibling {
        int z_index = 0;
        std::uint32_t age = 0;
        entt::entity entity{entt::null};
    };

    template <typename View, typename ZIndex>
    void build(const entt::registry& registry, const HierarchyIndex& index, const View& matching, const ZIndex& z_of) {
        static_assert(std::is_same_v<std::decay_t<std::invoke_result_t<const ZIndex&, entt::entity>>, int>);
        preorder_.clear();
        stack_.clear();
        fill_hierarchy_roots(registry, index, matching, ordered_, roots_);
        for (const auto root : roots_) {
            stack_.push_back(root);
            while (!stack_.empty()) {
                const auto entity = stack_.back();
                stack_.pop_back();
                positions_.set(entity, static_cast<std::uint32_t>(preorder_.size()));
                preorder_.push_back(entity);
                siblings_.clear();
                index.for_each_child(entity, [&](entt::entity child) {
                    if (matching.contains(child)) {
                        siblings_.push_back(
                            {.z_index = z_of(child), .age = static_cast<std::uint32_t>(siblings_.size()), .entity = child});
                    }
                });
                std::ranges::sort(siblings_, {}, [](const Sibling& sibling) {
                    return std::pair{sibling.z_index, sibling.age};
                });
                for (const auto& sibling : std::views::reverse(siblings_)) {
                    stack_.push_back(sibling.entity);
                }
            }
        }
        built_ = true;
    }

    // Positions of entities no longer in the forest stay behind; preorder_ disowns them.
    SparseSlots positions_;
    std::vector<entt::entity> preorder_;
    std::vector<entt::entity> stack_;
    std::vector<Sibling> siblings_;
    std::vector<std::pair<std::uint64_t, entt::entity>> ordered_;
    std::vector<entt::entity> roots_;
    int passes_  = 0;
    bool built_  = false;
};

// ── Sweep-and-prune broad phase (spatial-broadphase-runtime capability) ────────
// Runtime-owned, program-independent bipartite 2D/3D broad phase over
// axis-aligned bounds. Each proxy belongs to the left or right binding of a
// pair rule; an entity in both memberships has one proxy per side. Candidates
// are conservative (no false negatives), left-right only, and deterministic.

enum class SapSide : std::uint8_t { Left, Right };

struct ProxyAabb2D {
    entt::entity entity{entt::null};
    std::uint64_t ordinal{0};
    SapSide side{SapSide::Left};
    Vector2 min{};
    Vector2 max{};
};

struct ProxyAabb3D {
    entt::entity entity{entt::null};
    std::uint64_t ordinal{0};
    SapSide side{SapSide::Left};
    Vector3 min{};
    Vector3 max{};
};

// Bounds for the recognized overlap predicates' shapes. A radius bounds by its
// magnitude; a box by the axis-aligned bounds of its rotated half extents.
[[nodiscard]] ProxyAabb2D
circle_proxy(entt::entity entity, std::uint64_t ordinal, SapSide side, Vector2 center, float radius) noexcept;
[[nodiscard]] ProxyAabb3D
sphere_proxy(entt::entity entity, std::uint64_t ordinal, SapSide side, Vector3 center, float radius) noexcept;
[[nodiscard]] ProxyAabb3D box_proxy(
    entt::entity entity, std::uint64_t ordinal, SapSide side, Vector3 center, Vector3 size, Quat rotation) noexcept;
// An unrotated box: center plus or minus half the size's magnitudes.
[[nodiscard]] ProxyAabb3D
aligned_box_proxy(entt::entity entity, std::uint64_t ordinal, SapSide side, Vector3 center, Vector3 size) noexcept;
// The segment from origin along direction for max_distance. A segment
// std.collision.volume.ray_box would reject outright (no length, no direction
// or a non-finite value) gets empty bounds, which overlap nothing.
[[nodiscard]] ProxyAabb3D segment_proxy(entt::entity entity,
                                        std::uint64_t ordinal,
                                        SapSide side,
                                        Vector3 origin,
                                        Vector3 direction,
                                        float max_distance) noexcept;

// Where each entity sits in a pass snapshot. Spawned entities take indices near
// the top of the index space, so a table sized by entity index would be huge.
class SnapshotPositions {
public:
    static constexpr std::uint32_t kAbsent = std::numeric_limits<std::uint32_t>::max();

    explicit SnapshotPositions(const std::vector<entt::entity>& snapshot) {
        positions_.reserve(snapshot.size());
        for (std::size_t position = 0; position < snapshot.size(); ++position) {
            positions_.emplace(snapshot[position], static_cast<std::uint32_t>(position));
        }
    }

    [[nodiscard]] std::uint32_t find(entt::entity entity) const noexcept {
        const auto found = positions_.find(entity);
        return found == positions_.end() ? kAbsent : found->second;
    }

private:
    std::unordered_map<entt::entity, std::uint32_t> positions_;
};

// Bounds of a collider descriptor, inflated by the contact skin and covering
// its motion over `delta`. No descriptor, no proxy: such an entity can only miss.
[[nodiscard]] std::optional<ProxyAabb3D> collider_proxy(entt::entity entity,
                                                        std::uint64_t ordinal,
                                                        SapSide side,
                                                        const std::optional<physics::ColliderShape>& shape,
                                                        Vector3 delta = {}) noexcept;

// Indices into the proxy span passed to the most recent sync(): `left` is a
// left-side proxy and `right` a right-side one. Sorted by (left ordinal,
// right ordinal), the order of the unaccelerated left-major product.
struct SapCandidatePair {
    std::size_t left;
    std::size_t right;
};

// Benchmark-derived default small-domain threshold on the left x right
// product size, below which bounds are tested pairwise: the two strategies
// tie at 64 x 64 and the sweep is 1.4x faster at 128 x 128 (hidden
// "[benchmark]" case). Internal and revisable; correctness never depends on it.
inline constexpr std::size_t kSapDefaultSmallDomainThreshold = 4096;

// Global test-only override for every SapBroadPhase2D/3D instance's
// small-domain threshold: generated code builds its broad phase locally,
// out of a test's reach. nullopt leaves each instance's own threshold.
[[nodiscard]] std::optional<std::size_t> sap_small_domain_threshold_override_for_testing() noexcept;
void set_sap_small_domain_threshold_override_for_testing(std::optional<std::size_t> threshold) noexcept;

class SapBroadPhase2D {
public:
    void sync(std::span<const ProxyAabb2D> proxies);
    [[nodiscard]] std::span<const SapCandidatePair> candidate_pairs() const noexcept;

    [[nodiscard]] int primary_axis_for_testing() const noexcept;
    void set_small_domain_threshold_for_testing(std::size_t threshold) noexcept;

private:
    std::vector<SapCandidatePair> candidates_;
    int primary_axis_{0};
    std::size_t small_domain_threshold_{kSapDefaultSmallDomainThreshold};
};

class SapBroadPhase3D {
public:
    void sync(std::span<const ProxyAabb3D> proxies);
    [[nodiscard]] std::span<const SapCandidatePair> candidate_pairs() const noexcept;

    [[nodiscard]] int primary_axis_for_testing() const noexcept;
    void set_small_domain_threshold_for_testing(std::size_t threshold) noexcept;

private:
    std::vector<SapCandidatePair> candidates_;
    int primary_axis_{0};
    std::size_t small_domain_threshold_{kSapDefaultSmallDomainThreshold};
};

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
