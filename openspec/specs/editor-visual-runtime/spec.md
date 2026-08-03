## Purpose
Define the visual rendering behavior of the in-game editor, including render-phase placement of editor extern rules, gizmo drawing, template palette UI, edit-mode overlay, and world-space input helpers.

## Requirements

### Requirement: Editor extern render rules run in the render phase
`GizmoRenderer2D`, `GizmoRenderer3D`, `EditorTemplatePalette`, and `EditorPropertyPanel` SHALL be recognized as render-phase extern rules by `is_render_phase_extern` in the codegen. They SHALL be emitted as calls within the render-frame flush boundary (the code path between `begin_render_frame` and `end_render_frame`) — inside `generated_render_project` for the legacy main loop, or inside the render phase activation's dispatch for the graph-driven main loop — not in `generated_update_project` or any non-render phase activation.

#### Scenario: Editor rules placed in render phase (legacy main loop)
- **WHEN** a module imports `std.editor`, declares `GizmoRenderer2D` and `EditorTemplatePalette` as extern rules, and is generated with the legacy main loop
- **THEN** the generated `generated_render_project` contains calls to `gizmo_renderer2_d_tick` and `editor_template_palette_tick`
- **THEN** the generated `generated_update_project` does NOT contain calls to `gizmo_renderer2_d_tick` or `editor_template_palette_tick`

#### Scenario: Editor rules placed in render phase (graph-driven main loop)
- **WHEN** a module imports `std.editor`, declares `GizmoRenderer2D` and `EditorTemplatePalette` as extern rules, and is generated with a non-empty execution graph and a resolved external frame event (graph-driven main loop)
- **THEN** the render phase activation's dispatch contains calls to `gizmo_renderer2_d_tick` and `editor_template_palette_tick`, wrapped by that frame's `begin_render_frame`/`end_render_frame` calls
- **THEN** no other phase activation contains calls to `gizmo_renderer2_d_tick` or `editor_template_palette_tick`

### Requirement: GizmoRenderer2D draws Blender-style selection and transform handles
When `EditorState.active` is true, `GizmoRenderer2D` SHALL open a `BeginMode2D(get_active_camera_2d())` block and draw the following for each entity that has both `EditorGizmo2D` and `WorldTransform`:

- **Always**: A rectangle outline (`DrawRectangleLinesEx`) around the entity's AABB. The AABB uses `BoxCollider.size` if present, otherwise a 1×1 world-unit square centered at `WorldTransform.position`. The outline color SHALL be the `EditorGizmo2D.color` field.
- **mode=1 (Translate)**: A red arrow along +X and a green arrow along +Y, each of length `EditorGizmo2D.size` world units, with a filled triangle arrowhead. Arrow shaft drawn with `DrawLineEx`.
- **mode=2 (Rotate)**: A cyan ring (`DrawRing`) centered at `WorldTransform.position` with inner radius `EditorGizmo2D.size * 0.8` and outer radius `EditorGizmo2D.size`.
- **mode=3 (Scale)**: Red and green lines along +X and +Y with a small filled square (`DrawRectangleV`) at the tip of each.

All coordinates are in world space. The `BeginMode2D` camera transform maps them to screen pixels.

#### Scenario: Selection outline in edit mode
- **WHEN** an entity has `EditorSelected` and `EditorGizmo2D` with mode=1 and `EditorState.active=true`
- **THEN** the render phase draws a rectangle outline around the entity and a red+green arrow pair

#### Scenario: No gizmos outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** `GizmoRenderer2D` draws nothing (early return)

#### Scenario: Rotate gizmo
- **WHEN** an entity has `EditorGizmo2D` with mode=2
- **THEN** the render phase draws a cyan ring at the entity's world position

### Requirement: EditorTemplatePalette draws screen-space template buttons and handles clicks
`EditorTemplatePalette` SHALL iterate `cactus_template_registry` in the render phase and draw one button per `pub template` along the left screen edge (starting at screen position (10, 40), each button 140×26px with 4px gap). Each button SHALL have a deterministic tint color derived from `std::hash<std::string>` of the template name, mapped to a hue in HSL space with fixed saturation=70% and lightness=55%. The template name SHALL be drawn as white text on the tinted button.

When `EditorState.active` is true and the left mouse button is pressed, if the cursor is within a button's rectangle, the rule SHALL:
1. Set `EditorState.active_template` to that template's name
2. Set `EditorState.mode` to 4 (Place)

The palette SHALL only be rendered (and handle input) when `EditorState.active` is true.

#### Scenario: Palette visible in edit mode
- **WHEN** `EditorState.active` is true and templates "Box" and "PlayerSpawn" are registered
- **THEN** two buttons are drawn at (10,40) and (10,70) with distinct tint colors

#### Scenario: Palette hidden outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** no palette buttons are drawn

#### Scenario: Clicking a template button
- **WHEN** `EditorState.active` is true and the user clicks on the "Box" button
- **THEN** `EditorState.active_template` is set to "Box" and `EditorState.mode` is set to 4

### Requirement: Edit-mode overlay drawn in render phase
After all other render-phase extern rules run, the codegen SHALL draw an edit-mode overlay when any EditorState entity has `active=true`, within the same render-frame flush boundary described above (`generated_render_project` under the legacy main loop, or the render phase activation's dispatch under the graph-driven main loop). The overlay consists of:
- A yellow rectangle outline 3px thick along the full screen boundary (`DrawRectangleLinesEx({0,0,screenW,screenH}, 3, YELLOW)`)
- A HUD text line at position (10, 10) in yellow showing the current mode: `"EDIT [<MODE>]  F1:toggle  W:trans  E:rot  R:scale  T:place"` where `<MODE>` is one of SELECT / TRANSLATE / ROTATE / SCALE / PLACE based on `EditorState.mode`

#### Scenario: Overlay visible in edit mode
- **WHEN** `EditorState.active` is true and `EditorState.mode` is 1
- **THEN** a yellow border and the text "EDIT [TRANSLATE]  F1:toggle  W:trans  E:rot  R:scale  T:place" are drawn

#### Scenario: Overlay hidden outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** no border and no HUD text are drawn

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

### Requirement: GizmoRenderer3D draws the edit-mode ground grid and selection gizmos
The emitted `GizmoRenderer3D` render-phase function SHALL early-return when no `EditorState` entity has `active=true`. When editing is active it SHALL open a `BeginMode3D(get_active_camera_3d())` block and draw, in order:

1. **Ground grid (always, independent of selection)**: `DrawGrid(20, 1.0F)` — a horizontal grid on the y=0 plane centered at the world origin. The grid SHALL be drawn even when no entity carries `EditorGizmo3D`.
2. **For each entity with both `EditorGizmo3D` and `WorldTransform` (vec3)**:
   - **Always**: a wire box (`DrawCubeWiresV`) around the entity. The box uses the entity's model bind-pose bounds scaled by `WorldTransform.scale` when the entity has `ModelRenderer` and its model resolves; otherwise a 1×1×1 world-unit cube centered at `WorldTransform.position`. The wire color SHALL be the `EditorGizmo3D.color` field.
   - **mode=1 (Translate)**: a red line along +X, a green line along +Y, and a blue line along +Z from the entity position, each of length `EditorGizmo3D.size` world units.
   - **mode=2 (Rotate)**: a cyan circle (`DrawCircle3D`) of radius `EditorGizmo3D.size` centered at the entity position, lying in the horizontal plane.
   - **mode=3 (Scale)**: the same three axis lines as Translate with a small cube (`DrawCubeV`) at the tip of each.

The block SHALL close with `EndMode3D()`.

#### Scenario: Grid visible in edit mode with empty selection
- **WHEN** `EditorState.active` is true and no entity has `EditorGizmo3D`
- **THEN** the render phase draws the y=0 grid and nothing else in the 3D gizmo pass

#### Scenario: No grid or gizmos outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** `GizmoRenderer3D` draws nothing (early return, no `BeginMode3D` block opened)

#### Scenario: Selected model gets a wire box sized from its bounds
- **WHEN** an entity has `EditorGizmo3D`, `WorldTransform`, and `ModelRenderer` with a loaded model, and `EditorState.active` is true
- **THEN** the render phase draws a wire box matching the model's bind-pose bounds scaled by the entity's transform scale

#### Scenario: Translate mode draws three axis lines
- **WHEN** an entity has `EditorGizmo3D` with mode=1 and `EditorState.active` is true
- **THEN** the render phase draws red (+X), green (+Y), and blue (+Z) axis lines at the entity's position

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
