## Context

The Cactus DSL compiles to a C++ / EnTT / Raylib backend. The current 2D camera flow is:
1. Codegen detects `std.camera.flat` import and emits a camera-sync block at the top of `generated_update_project`.
2. The sync block iterates `registry.view<Camera>()` and calls `set_active_camera_2d()` for the first entity where `Camera.active == true`.
3. All render systems (`ShapeRenderer`, `SpriteRenderer`, etc.) call `get_active_camera_2d()` to enter the BeginMode2D block. The 3D path is symmetric.

This yields exactly one active camera per frame and no way to render a second viewpoint.

## Goals / Non-Goals

**Goals:**
- Support N simultaneous viewports (split-screen, mini-map, picture-in-picture, etc.)
- Viewport rect in normalized 0–1 space (resolution-independent)
- Viewport ordered by `depth` (lower depth drawn first = underneath)
- Render systems unchanged — they still read `get_active_camera_*()` per call
- 2D and 3D cameras both supported through the same Viewport mechanism

**Non-Goals:**
- Render-layer / culling-mask filtering (entities in all viewports, always)
- Editor multi-viewport support (editor still operates on a single active viewport)
- Hot-reload or dynamic add/remove of viewports at runtime beyond normal ECS entity operations

## Decisions

### D1: Viewport is a separate trait composed with Camera on the same entity

An entity that renders a viewport must have both:
- A `std.camera.Viewport` trait (screen placement + depth + clear settings)
- Either `std.camera.flat.Camera` (2D) or `std.camera.volume.Camera` (3D) for the view transform

Alternatives considered:
- Embed rect inside Camera: breaks the single-responsibility; you'd need rect on both flat and volume Camera, duplicating it.
- Separate Viewport entity that references a Camera entity: adds an indirection (entity-by-id lookup, which the language doesn't yet have), complicates codegen.
- Verdict: composition on one entity is idiomatic ECS and matches the existing flat/volume Camera pattern.

### D2: Normalized rect via four explicit fields, not vec4

```
pub trait Viewport:
    var x:      float = 0.0
    var y:      float = 0.0
    var width:  float = 1.0
    var height: float = 1.0
    var depth:  int   = 0
    var clear:  bool  = true
    var clear_color: color = #000000FF
    var active: bool  = true
```

`vec4` was considered for compactness but the x/y/w/h convention is not self-documenting. Explicit fields are consistent with how traits are expressed elsewhere in the stdlib.

### D3: Remove `active: bool` from Camera — Viewport.active replaces it

The current `Camera.active` was only ever used to select "the one active camera." With Viewport owning that selection, having two `active` fields on one entity is confusing and redundant. Removing it is a breaking change but we have no backward-compat requirement.

An entity is an active camera IFF:
- It has a `Viewport` trait with `active = true`
- It has a Camera trait (flat or volume) for the view transform

### D4: Viewport module lives at `std.camera` root (new `viewport.cactus`)

Location: `stdlib/std/camera/viewport.cactus`, module `std.camera.viewport`.

Rationale: Viewport is shared between flat (2D) and volume (3D) cameras. Placing it inside `std.camera.flat` or `std.camera.volume` would create an asymmetric dependency. A dedicated module at the camera root is clean and importable independently.

### D5: Codegen emits a viewport render loop in `generated_render_project`

When codegen detects that the project imports `std.camera.viewport`, it replaces the flat camera-sync block with a viewport loop:

```cpp
// generated_render_project (pseudocode)
begin_render_frame();

// Collect viewports sorted by depth
std::vector<std::pair<int,entt::entity>> vps;
for (auto [e, vp] : registry.view<Viewport>().each()) {
    if (vp.active) vps.push_back({vp.depth, e});
}
std::sort(vps.begin(), vps.end());

for (auto [depth, e] : vps) {
    auto& vp = registry.get<Viewport>(e);
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    BeginScissorMode(vp.x * sw, vp.y * sh, vp.width * sw, vp.height * sh);
    if (vp.clear) ClearBackground(to_raylib(vp.clear_color));

    // Set active camera from the same entity's Camera trait
    if (registry.all_of<flat::Camera>(e)) {
        auto& cam = registry.get<flat::Camera>(e);
        set_active_camera_2d(translate_camera_2d(cam, sw, sh));
    } else if (registry.all_of<volume::Camera>(e)) {
        auto& cam = registry.get<volume::Camera>(e);
        set_active_camera_3d(translate_camera_3d(e, cam));  // uses Transform position
    }

    // render systems called here — they read get_active_camera_*()
    shape_renderer_tick(registry);
    sprite_renderer_tick(registry);
    // ...

    EndScissorMode();
}

end_render_frame();
```

The camera-sync block in `generated_update_project` is removed entirely. Active-camera setting now only happens inside the viewport render loop.

## Risks / Trade-offs

- **Editor hit-test is viewport-unaware** → `editor_screen_to_world_2d` uses `get_active_camera_2d()`, which returns the last set camera. In multi-viewport scenes, this may be the wrong camera. Mitigation: deferred; editor currently targets single-viewport use.
- **Scissor mode interacts with Raylib's own render pass structure** → `BeginScissorMode` affects all subsequent draw calls including UI. If render systems emit screen-space HUD elements, they'll be scissor-clipped. Mitigation: editor overlay and HUD systems should run outside the viewport loop.
- **Sorting viewports by depth each frame** → Small overhead proportional to viewport count. In practice N < 8. Not a concern.
- **No layer mask** → All entities render in every viewport. A mini-map will show everything the main camera sees. Mitigation: acceptable for initial design; layer masks can be added later as a separate Viewport field.

## Migration Plan

1. Add `stdlib/std/camera/viewport.cactus` with `Viewport` trait
2. Remove `active: bool` from `std.camera.flat.Camera` and `std.camera.volume.Camera`
3. Update codegen: remove camera-sync block from `generated_update_project`; emit viewport render loop in `generated_render_project`
4. Update specs: `stdlib-camera`, `editor-camera-2d`
5. Projects using `Camera.active = true` must add a `Viewport` entity instead

No rollback strategy needed (no backward compat requirement).

## Open Questions

- Should `std.camera.viewport` be a module users import explicitly, or is it auto-imported when `std.camera.flat` is imported?
  - Current lean: explicit import (`use std.camera.viewport as vp`), matching how other traits work.
- When both a flat and a volume Camera are on the same viewport entity, which wins?
  - Current lean: flat wins (2D over 3D) or it's a compile error. TBD in spec.
