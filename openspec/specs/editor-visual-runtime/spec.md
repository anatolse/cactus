## Purpose
Define the visual rendering behavior of the in-game editor, including render-phase placement of editor extern systems, gizmo drawing, template palette UI, edit-mode overlay, and world-space input helpers.

## Requirements

### Requirement: Editor extern render systems run in the render phase
`GizmoRenderer2D`, `GizmoRenderer3D`, `EditorTemplatePalette`, and `EditorPropertyPanel` SHALL be recognized as render-phase extern systems by `is_render_phase_extern` in the codegen. They SHALL be emitted as calls inside `generated_render_project` (between `begin_render_frame` and `end_render_frame`), not in `generated_update_project`.

#### Scenario: Editor systems placed in render phase
- **WHEN** a module imports `std.editor` and declares `GizmoRenderer2D` and `EditorTemplatePalette` as extern systems
- **THEN** the generated `generated_render_project` contains calls to `gizmo_renderer2_d_tick` and `editor_template_palette_tick`
- **THEN** the generated `generated_update_project` does NOT contain calls to `gizmo_renderer2_d_tick` or `editor_template_palette_tick`

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

When `EditorState.active` is true and the left mouse button is pressed, if the cursor is within a button's rectangle, the system SHALL:
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
After all other render-phase extern systems run, the codegen-emitted `generated_render_project` SHALL draw an edit-mode overlay when any EditorState entity has `active=true`. The overlay consists of:
- A yellow rectangle outline 3px thick along the full screen boundary (`DrawRectangleLinesEx({0,0,screenW,screenH}, 3, YELLOW)`)
- A HUD text line at position (10, 10) in yellow showing the current mode: `"EDIT [<MODE>]  F1:toggle  W:trans  E:rot  R:scale  T:place"` where `<MODE>` is one of SELECT / TRANSLATE / ROTATE / SCALE / PLACE based on `EditorState.mode`

#### Scenario: Overlay visible in edit mode
- **WHEN** `EditorState.active` is true and `EditorState.mode` is 1
- **THEN** a yellow border and the text "EDIT [TRANSLATE]  F1:toggle  W:trans  E:rot  R:scale  T:place" are drawn

#### Scenario: Overlay hidden outside edit mode
- **WHEN** `EditorState.active` is false
- **THEN** no border and no HUD text are drawn

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
