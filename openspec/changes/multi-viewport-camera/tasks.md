## 1. Stdlib: Viewport Trait Module

- [x] 1.1 Create `stdlib/std/camera/viewport.cactus` with `module std.camera.viewport` declaration
- [x] 1.2 Add `pub trait Viewport` with fields: `x`, `y`, `width`, `height` (float, defaults 0/0/1/1), `depth: int = 0`, `clear: bool = true`, `clear_color: color = #000000FF`, `active: bool = true`

## 2. Stdlib: Remove `active` from Camera Traits

- [x] 2.1 Remove `var active: bool = true` from `stdlib/std/camera/flat.cactus` Camera trait
- [x] 2.2 Remove `var active: bool = true` from `stdlib/std/camera/volume.cactus` Camera trait

## 3. Codegen: Viewport Render Loop

- [x] 3.1 Remove camera-sync block emission from `generated_update_project` codegen path entirely
- [x] 3.2 Emit viewport render loop in `generated_render_project` between `begin_render_frame` and `end_render_frame` when `std.camera.viewport` is imported
- [x] 3.4 Viewport loop: collect active Viewport entities, sort by `depth`, iterate; emit `BeginScissorMode` / `EndScissorMode` with denormalized rect
- [x] 3.5 Viewport loop: emit `if (vp.clear) ClearBackground(...)` per viewport before render systems
- [x] 3.6 Viewport loop: for entities with `std.camera.flat.Camera`, emit `set_active_camera_2d(translate_camera_2d(...))` call before render systems
- [x] 3.7 Viewport loop: for entities with `std.camera.volume.Camera` + `std.transform.volume.Transform`, emit `set_active_camera_3d(translate_camera_3d(...))` call before render systems

## 4. Backend Runtime: translate_camera helpers

- [x] 4.1 Implement `translate_camera_2d(flat::Camera, screenW, screenH) → Camera2D` in cpp-entt backend (same logic as existing camera-sync block)
- [x] 4.2 Implement `translate_camera_3d(entity, volume::Camera, registry) → Camera3D` in cpp-entt backend (reads entity's volume Transform for position)

## 5. Spec Sync

- [x] 5.1 Run `openspec sync specs` to apply delta specs from `stdlib-viewport`, `stdlib-camera`, `editor-camera-2d` into their canonical locations under `openspec/specs/` (applied manually: created stdlib-viewport/spec.md, updated stdlib-camera/spec.md and editor-camera-2d/spec.md)

## 6. Tests

- [x] 6.1 Add codegen test: module with `std.camera.viewport` emits viewport loop in `generated_render_project` and no camera-sync block in `generated_update_project`
- [x] 6.3 Add runtime adapter test: two Viewport entities with different depths render in correct order
- [x] 6.4 Add runtime adapter test: Viewport with `clear=true` clears region; `clear=false` does not
- [x] 6.5 Add runtime adapter test: split-screen (two Viewport entities each covering half the screen) — each uses its own camera transform
- [x] 6.6 Verify existing stdlib-camera tests still pass (FollowCamera, FirstPersonCamera, ThirdPersonCamera — unchanged behavior)

## 7. Example Update (optional)

- [x] 7.1 Update or create an example that demonstrates split-screen by composing two Viewport + Camera entities
