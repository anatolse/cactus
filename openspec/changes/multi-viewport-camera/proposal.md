## Why

The current camera system supports exactly one active camera per frame, selected by a boolean flag on the Camera trait. This makes split-screen, mini-maps, and any multi-view layout impossible to express — fundamental use cases for a game engine.

## What Changes

- **New** `std.camera` root module with a `Viewport` trait: normalized screen-space rect (0–1), render depth, clear settings.
- **BREAKING** Remove `active: bool` from `std.camera.flat.Camera` and `std.camera.volume.Camera`. Viewport presence replaces the active-camera selection mechanism.
- Codegen detects `std.camera.Viewport` usage and emits a **per-viewport render loop** in `generated_render_project` instead of a single global camera-sync block. Each iteration sets the active-camera global before calling render systems.
- Render systems (`ShapeRenderer`, `SpriteRenderer`, `MeshRenderer`) remain unchanged — they still read `get_active_camera_*()` per call. The loop provides the context.
- `editor_screen_to_world_2d` and `editor_mouse_delta_2d` continue to use `get_active_camera_2d()`, which is now valid within a viewport render context.

## Capabilities

### New Capabilities
- `stdlib-viewport`: The `std.camera.Viewport` trait — normalized rect, depth ordering, clear settings. Also covers the codegen-emitted viewport render loop that replaces the single camera-sync block.

### Modified Capabilities
- `stdlib-camera`: Remove `active: bool` from both `std.camera.flat.Camera` and `std.camera.volume.Camera`. Update active-camera-selection semantics: an entity is an active camera if and only if it has a `Viewport` trait with `active = true`.
- `editor-camera-2d`: The camera-sync block at the top of `generated_update_project` is replaced by per-viewport camera setting within the render loop. The `set_active_camera_2d` / `get_active_camera_2d` contract is unchanged, but the call site moves.

## Impact

- `stdlib/std/camera/flat.cactus` — remove `active` field from Camera trait
- `stdlib/std/camera/volume.cactus` — remove `active` field from Camera trait
- `stdlib/std/camera/` — new `viewport.cactus` (or root `camera.cactus`) for Viewport trait
- Codegen — viewport render loop emission (replaces camera-sync block detection)
- Existing user code that sets `Camera.active = true` will break; migration is to add a `Viewport` entity composition instead
- `openspec/specs/stdlib-camera/spec.md` — update camera-activation semantics
- `openspec/specs/editor-camera-2d/spec.md` — update camera-sync block location and trigger condition
