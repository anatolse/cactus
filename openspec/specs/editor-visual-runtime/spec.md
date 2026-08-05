## Purpose
Define the visual rendering behavior of the in-game editor, including render-phase placement of editor extern rules, gizmo drawing, template palette UI, edit-mode overlay, and world-space input helpers.

## Requirements

### Requirement: Editor render-phase rules run in the render phase
`GizmoRenderer2D`, `GizmoRenderer3D`, and `EditorTemplatePalette` are plain DSL rules (see `stdlib-editor`) using the standard `on render:` phase, not compiler-owned extern rules dispatched by name. They SHALL be scheduled within the render-frame flush boundary (the code path between `begin_render_frame` and `end_render_frame`) — inside `generated_render_project` for the legacy main loop, or inside the render phase activation's dispatch for the graph-driven main loop — the same as any other rule declaring `on render:`. No name-matching codegen dispatch (`is_render_phase_extern`/`is_editor_extern_system`) is required for them.

`EditorPropertyPanel` remains a compiler-owned `extern rule` and continues to require explicit render-phase recognition, unchanged from prior behavior.

#### Scenario: Editor rules placed in render phase (legacy main loop)
- **WHEN** a module imports `std.editor` and is generated with the legacy main loop
- **THEN** the generated `generated_render_project` contains calls reachable from `GizmoRenderer2D` and `EditorTemplatePalette`'s compiled handler functions
- **THEN** the generated `generated_update_project` does NOT contain calls to those handler functions

#### Scenario: Editor rules placed in render phase (graph-driven main loop)
- **WHEN** a module imports `std.editor` and is generated with a non-empty execution graph and a resolved external frame event (graph-driven main loop)
- **THEN** the render phase activation's dispatch contains calls reachable from `GizmoRenderer2D` and `EditorTemplatePalette`'s compiled handler functions, wrapped by that frame's `begin_render_frame`/`end_render_frame` calls
- **THEN** no other phase activation contains calls to those handler functions

### Requirement: GizmoRenderer2D emits debug-draw primitives for selection and transform handles
`GizmoRenderer2D` (see `stdlib-editor`) SHALL compute Blender-style selection and transform handle geometry in DSL and SHALL emit `std.debug`/`std.ui` primitive events (per `editor-debug-draw`) to draw it, rather than the backend hardcoding the geometry decision. The generic event renderers SHALL open the `BeginMode2D(get_active_camera_2d())` block for world-space primitives.

#### Scenario: Selection outline in edit mode
- **WHEN** an entity has `EditorSelected` and `EditorState.active=true`
- **THEN** the render phase draws a rectangle outline around the entity's AABB (`DrawDebugRectOutline2D`) and, in Translate mode, a red+green arrow pair (`DrawDebugLine2D` + `DrawDebugTriangle2D`)

#### Scenario: No gizmos outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** `GizmoRenderer2D` emits no debug-draw events (early return in the DSL rule)

#### Scenario: Rotate gizmo
- **WHEN** an entity has `EditorSelected` and `EditorState.mode` is `GizmoMode.Rotate`
- **THEN** the render phase draws a cyan ring (`DrawDebugRingOutline2D`) at the entity's world position

### Requirement: EditorTemplatePalette draws screen-space template buttons and handles clicks
`EditorTemplatePalette` (see `stdlib-editor`) SHALL iterate `template_names()` in a DSL `for` loop and SHALL emit `DrawScreenRect` + `ScreenLabel` updates for one button per registered `pub template`, along the left screen edge (starting at screen position (10, 40), each button 140×26px with 4px gap). Each button's tint color SHALL be assigned deterministically by its index in the returned list (per `stdlib-editor`), not derived from the template name.

When `EditorState.active` is true and the left mouse button is pressed, if the cursor is within a button's rectangle, the DSL rule itself SHALL:
1. Set `EditorState.active_template` to that template's name
2. Set `EditorState.mode` to `GizmoMode.Place`

The palette SHALL only emit its draw events and handle input when `EditorState.active` is true.

#### Scenario: Palette visible in edit mode
- **WHEN** `EditorState.active` is true and templates "Box" and "PlayerSpawn" are registered
- **THEN** two buttons are drawn at (10,40) and (10,70) with distinct index-derived tint colors

#### Scenario: Palette hidden outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** no palette buttons are drawn

#### Scenario: Clicking a template button
- **WHEN** `EditorState.active` is true and the user clicks on the "Box" button
- **THEN** `EditorState.active_template` is set to "Box" and `EditorState.mode` is set to `GizmoMode.Place`

### Requirement: Edit-mode overlay drawn by a DSL rule in the render phase
The edit-mode overlay SHALL be drawn by `EditorHUDOverlay` (see `stdlib-editor`), an ordinary DSL rule, rather than being spliced unconditionally into codegen. It SHALL draw, when any `EditorState` entity has `active=true`:
- A yellow rectangle outline 3px thick along the full screen boundary, via `DrawScreenRect` (outline, using `screen_size()`)
- A HUD text line at position (10, 10) in yellow showing the current mode, via a `ScreenLabel` built with `text.format`: `"EDIT [<MODE>]  F1:toggle  W:trans  E:rot  R:scale  T:place"` where `<MODE>` is one of SELECT / TRANSLATE / ROTATE / SCALE / PLACE based on `EditorState.mode`

#### Scenario: Overlay visible in edit mode
- **WHEN** `EditorState.active` is true and `EditorState.mode` is 1
- **THEN** a yellow border (`DrawScreenRect`) and the text "EDIT [TRANSLATE]  F1:toggle  W:trans  E:rot  R:scale  T:place" (`ScreenLabel`) are drawn

#### Scenario: Overlay hidden outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** no border is drawn and the HUD `ScreenLabel` is not visible

#### Scenario: Overlay renders under the graph-driven main loop
- **WHEN** a program using `std.editor` with `EditorState.active = true` is generated with a non-empty execution graph and a resolved external frame event (graph-driven main loop)
- **THEN** the render phase activation's dispatch draws the yellow border and HUD text every frame, the same as under the legacy main loop

### Requirement: editor_hit_test_2d performs AABB test in world space
`editor_hit_test_2d(screen_pos, mask)` SHALL convert `screen_pos` to world space using `editor_screen_to_world_2d`, then iterate all entities with `WorldTransform` and `BoxCollider`, and return the first entity whose AABB (position to position+size) contains the world point and whose `Collider.layer & mask != 0` (if `Collider` component is present; otherwise always matches). Entities with `EditorLocked` SHALL be excluded. Returns `entt::null` if no match.

#### Scenario: Click hits unlocked entity
- **WHEN** the cursor is over an entity with `BoxCollider` that does not have `EditorLocked`
- **THEN** `editor_hit_test_2d` returns that entity's handle

#### Scenario: Click misses locked entity
- **WHEN** the cursor is over an entity with `EditorLocked`
- **THEN** `editor_hit_test_2d` returns `entt::null`

#### Scenario: Click hits nothing
- **WHEN** the cursor is not over any entity with `BoxCollider`
- **THEN** `editor_hit_test_2d` returns `entt::null`

### Requirement: editor_screen_to_world_2d applies inverse camera transform
`editor_screen_to_world_2d(screen)` SHALL return `(screen - camera.offset) / camera.zoom + camera.target`, where `camera` is the current `get_active_camera_2d()` value.

#### Scenario: Identity camera leaves position unchanged
- **WHEN** `get_active_camera_2d()` returns identity (zoom=1, offset=(0,0), target=(0,0))
- **THEN** `editor_screen_to_world_2d({400, 300})` returns `{400, 300}`

#### Scenario: Zoomed camera converts correctly
- **WHEN** the active camera has zoom=64, offset=(400,300), target=(0,0) (world origin at screen center)
- **THEN** `editor_screen_to_world_2d({400, 300})` returns `{0, 0}`
- **THEN** `editor_screen_to_world_2d({464, 300})` returns approximately `{1, 0}`

### Requirement: editor_mouse_delta_2d returns world-space mouse movement
`editor_mouse_delta_2d()` SHALL return `GetMouseDelta()` divided by `get_active_camera_2d().zoom`, converting pixel delta to world-unit delta.

#### Scenario: Delta scales with zoom
- **WHEN** the active camera zoom is 64.0 and the mouse moved 64 pixels horizontally
- **THEN** `editor_mouse_delta_2d()` returns approximately `{1.0, 0.0}`

### Requirement: GizmoRenderer3D emits debug-draw primitives for the ground grid and selection gizmos
The ground grid SHALL remain a dedicated, always-on generic extern rule gated on `EditorCamera3D` existence (per `editor-debug-draw`), independent of `GizmoRenderer3D`. `GizmoRenderer3D` (see `stdlib-editor`) SHALL compute per-entity gizmo geometry in DSL and SHALL emit `std.debug` primitive events for it, rather than the backend hardcoding the geometry decision.

#### Scenario: Grid visible in edit mode with empty selection
- **WHEN** `EditorState.active` is true and no entity has `EditorGizmo3D`
- **THEN** the render phase draws the y=0 grid (via the dedicated grid extern rule) and nothing else in the 3D gizmo pass

#### Scenario: No grid or gizmos outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** no `EditorCamera3D` rig exists (per the existing camera lifecycle), so the grid extern rule draws nothing, and `GizmoRenderer3D` emits no debug-draw events

#### Scenario: Selected entity gets a fixed-size wire box
- **WHEN** an entity has `EditorSelected` and `std.transform.volume.WorldTransform`, and `EditorState.active` is true
- **THEN** the render phase draws a 1×1×1 wire box (`DrawDebugWireBox3D`) centered on the entity's position

Fixed-size rather than `ModelRenderer`-bounds-scaled: see `editor-declarative-rendering`'s design notes (decision 8) — reading optional `ModelRenderer`/`BoxCollider` bounds from DSL would require `std.editor` to `use std.render.models`/`use std.physics.flat`, an untested cross-module dependency risk given `std.editor` already needs dedicated codegen special-casing to keep its existing unconditional `std.transform.flat`/`std.transform.volume` imports from leaking the wrong dimension's extern rule into flat-only or volume-only programs.

#### Scenario: Translate mode draws three axis lines
- **WHEN** an entity has `EditorSelected` with `EditorState.mode` = `GizmoMode.Translate` and `EditorState.active` is true
- **THEN** the render phase draws red (+X), green (+Y), and blue (+Z) axis lines (`DrawDebugLine3D`) at the entity's position

### Requirement: editor_plane_project_3d intersects the cursor ray with a plane
`editor_plane_project_3d(screen, plane_origin, plane_normal)` SHALL build a picking ray from the active 3D camera through `screen` (raylib `GetScreenToWorldRay` or equivalent) and return the ray/plane intersection point. When the ray is parallel to the plane (|denominator| below epsilon) or the intersection lies behind the ray origin (`t < 0`), it SHALL return `plane_origin`.

#### Scenario: Center-screen click with a downward-looking camera hits the ground
- **WHEN** the active 3D camera is at `(0, 10, 0)` looking at the origin and `editor_plane_project_3d` is called with the screen center, plane origin `(0,0,0)`, and normal `(0,1,0)`
- **THEN** the returned point is approximately `(0, 0, 0)`

#### Scenario: Ray parallel to plane falls back to plane origin
- **WHEN** the cursor ray direction is perpendicular to the plane normal
- **THEN** `editor_plane_project_3d` returns `plane_origin`

#### Scenario: Intersection behind the camera falls back to plane origin
- **WHEN** the ray/plane intersection parameter `t` is negative
- **THEN** `editor_plane_project_3d` returns `plane_origin`

### Requirement: editor_raycast_3d picks entities via a codegen-registered impl
The runtime SHALL expose `register_editor_raycast_impl` (mirroring `register_editor_hit_test_impl`); `editor_raycast_3d(screen_pos, mask)` SHALL delegate to the registered impl with the picking ray built from the active 3D camera, returning `entt::null` when no impl is registered. When the program declares a vec3 `WorldTransform` and `ModelRenderer`, codegen SHALL register an impl that tests the ray against each candidate entity's axis-aligned bounding box — the model's bind-pose bounds scaled by `WorldTransform.scale` and translated by `WorldTransform.position` — excluding entities with `EditorLocked`, and returns the hit with the smallest ray distance.

#### Scenario: Click hits the nearest model
- **WHEN** two entities with `ModelRenderer` overlap along the cursor ray
- **THEN** `editor_raycast_3d` returns the entity whose bounding-box intersection is nearest to the camera

#### Scenario: Locked entities are not pickable
- **WHEN** the cursor ray intersects only an entity with `EditorLocked`
- **THEN** `editor_raycast_3d` returns `entt::null`

#### Scenario: No impl registered
- **WHEN** the program has no vec3 `WorldTransform` or no `ModelRenderer` trait
- **THEN** `editor_raycast_3d` returns `entt::null`

### Requirement: editor_mouse_delta_3d returns ground-plane mouse movement
`editor_mouse_delta_3d()` SHALL project the current cursor position and the previous cursor position (current minus `GetMouseDelta()`) onto the y=0 plane via the same ray/plane intersection as `editor_plane_project_3d`, and return the difference as a world-space vector. When either projection falls back (parallel ray or negative `t`), it SHALL return the zero vector.

#### Scenario: Horizontal mouse movement maps to world-space delta
- **WHEN** the active 3D camera looks down at the ground plane and the mouse moves horizontally across the screen
- **THEN** `editor_mouse_delta_3d()` returns a non-zero vector lying in the y=0 plane

#### Scenario: Degenerate projection yields zero delta
- **WHEN** the cursor ray is parallel to the ground plane
- **THEN** `editor_mouse_delta_3d()` returns `(0, 0, 0)`
