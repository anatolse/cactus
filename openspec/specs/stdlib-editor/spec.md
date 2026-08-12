# Spec: std.editor module

## Purpose

The `std.editor` module provides the standard in-game editor infrastructure for Cactus projects. It exposes traits, enums, events, extern function bridges, and built-in rules that allow scene editing (selection, translation, placement, gizmo rendering) to be wired into any game that imports the module.

## Requirements

### Requirement: std.editor module exposes EditorState trait and singleton entity
The `std.editor` module SHALL declare a `pub trait EditorState` with fields `active: bool` (default `true`), `mode: GizmoMode` (default `GizmoMode.Select`), `selected: entity_id` (default `0`), `active_template: string` (default `""`), `focused_trait: string` (default `""`), `focused_field: string` (default `""`), and `use_3d: bool` (default `false`). The module SHALL declare a `pub entity Editor` with `EditorState` initialized to its defaults. The `use_3d` field selects which interaction dimension is active: 2D selection/placement rules run only when it is `false`, 3D selection/placement rules only when it is `true`.

#### Scenario: Editor entity exists in registry after module load
- **WHEN** a module imports `use std.editor` and the scene loads
- **THEN** exactly one entity with `EditorState` component exists in the registry
- **AND** the `EditorState.active` field is `true`
- **AND** the `EditorState.mode` field is `GizmoMode.Select`
- **AND** the `EditorState.selected` field is `0`
- **AND** the `EditorState.use_3d` field is `false`

#### Scenario: EditorState fields are writable by rules
- **WHEN** a rule filters on `EditorState as state`
- **THEN** the rule can read and write `state.active`, `state.mode`, `state.selected`, `state.active_template`, `state.focused_trait`, `state.focused_field`, and `state.use_3d`

### Requirement: std.editor module exposes GizmoMode enum
The `std.editor` module SHALL declare a `pub enum GizmoMode` with variants `Select`, `Translate`, `Rotate`, `Scale`, and `Place`.

#### Scenario: GizmoMode variants accessible after import
- **WHEN** authored code contains `use std.editor`
- **THEN** `GizmoMode.Select`, `GizmoMode.Translate`, `GizmoMode.Rotate`, `GizmoMode.Scale`, and `GizmoMode.Place` are valid enum values

### Requirement: std.editor module exposes selection marker traits
The `std.editor` module SHALL declare `pub trait EditorSelected`, `pub trait EditorLocked`, and `pub trait EditorHidden` as marker traits (no fields).

#### Scenario: EditorSelected can be added and removed at runtime
- **WHEN** a rule calls `add EditorSelected to entity` on an entity
- **THEN** the entity has the `EditorSelected` component
- **WHEN** a rule calls `remove EditorSelected from entity` on the same entity
- **THEN** the entity no longer has the `EditorSelected` component

#### Scenario: EditorLocked excludes entity from selection
- **WHEN** an entity has `EditorLocked` component
- **AND** an editor selection rule uses `exclude: EditorLocked` in its filter
- **THEN** the entity is not considered for selection

### Requirement: std.editor module exposes EditorSnap trait
The `std.editor` module SHALL declare a `pub trait EditorSnap` with fields `position_snap: float` (default `0.0`), `rotation_snap: float` (default `0.0`), and `scale_snap: float` (default `0.0`).

#### Scenario: EditorSnap fields are writable
- **WHEN** a rule filters on `EditorSnap as snap`
- **THEN** the rule can read and write `snap.position_snap`, `snap.rotation_snap`, and `snap.scale_snap`

#### Scenario: EditorSnap with position_snap=0 means no snap
- **WHEN** an entity has `EditorSnap` with `position_snap = 0.0`
- **THEN** a transform rule MAY skip position snapping for that entity

### Requirement: std.editor module exposes EditorCategory trait
The `std.editor` module SHALL declare a `pub trait EditorCategory` with fields `category: string` (let, no default) and `visible: bool` (var, default `true`).

#### Scenario: EditorCategory fields are readable after assignment
- **WHEN** an entity is created with `EditorCategory:` block setting `category = "Vegetation"` and `visible = true`
- **THEN** a rule filtering on `EditorCategory as cat` can read `cat.category` as `"Vegetation"` and `cat.visible` as `true`

### Requirement: std.editor module exposes EditorGizmo2D and EditorGizmo3D projection traits
The `std.editor` module SHALL declare `pub trait EditorGizmo2D` with fields `mode: GizmoMode`, `color: color`, and `size: float`. The module SHALL declare `pub trait EditorGizmo3D` with the same fields. Both traits SHALL be designed for frame-local projection via `project`.

#### Scenario: EditorGizmo2D can be projected onto a flat-world entity
- **WHEN** a rule calls `project EditorGizmo2D:` with `mode = GizmoMode.Translate`, `color = #00FF00FF`, `size = 1.0` on an entity with `std.transform.flat.WorldTransform`
- **THEN** the entity has projected `EditorGizmo2D` component during the current frame
- **AND** the projected component is cleared after the render pass

#### Scenario: EditorGizmo3D can be projected onto a volume-world entity
- **WHEN** a rule calls `project EditorGizmo3D:` with `mode = GizmoMode.Translate`, `color = #00FF00FF`, `size = 1.0` on an entity with `std.transform.volume.WorldTransform`
- **THEN** the entity has projected `EditorGizmo3D` component during the current frame
- **AND** the projected component is cleared after the render pass

### Requirement: std.editor module exposes editor events
The `std.editor` module SHALL declare `pub event EditorSelectionChanged` with fields `previous: entity_id` and `current: entity_id`. The module SHALL declare `pub event EditorModeChanged` with fields `previous_mode: GizmoMode` and `current_mode: GizmoMode`.

#### Scenario: EditorSelectionChanged event can be emitted
- **WHEN** a rule calls `emit EditorSelectionChanged:` with `previous = old_id` and `current = new_id`
- **THEN** rules with `on EditorSelectionChanged:` handlers receive the event

#### Scenario: EditorModeChanged event can be emitted
- **WHEN** a rule calls `emit EditorModeChanged:` with `previous_mode = GizmoMode.Select` and `current_mode = GizmoMode.Translate`
- **THEN** rules with `on EditorModeChanged:` handlers receive the event

### Requirement: std.editor module exposes extern func bridges
The `std.editor` module SHALL declare the following `pub extern func` declarations:
- `editor_spawn_template(template_name: string, position_2d: vec2, position_3d: vec3) entity_id`
- `editor_hit_test_2d(screen_pos: vec2, mask: int) entity_id`
- `editor_raycast_3d(screen_pos: vec2, mask: int) entity_id`
- `editor_screen_to_world_2d(screen: vec2) vec2`
- `editor_mouse_delta_2d() vec2`
- `editor_plane_project_3d(screen: vec2, plane_origin: vec3, plane_normal: vec3) vec3`
- `editor_mouse_delta_3d() vec3`
- `active_mode() GizmoMode` — returns the current `EditorState.mode` of the singleton `Editor` entity, for use by rules (such as the gizmo renderers) that are not filtered on `EditorState` itself and therefore have no other way to read it.
- `is_editor_active() bool` — returns the current `EditorState.active` of the singleton `Editor` entity, same narrow-accessor idiom as `active_mode()`. Needed because `EditorSelected` persists across an editor deactivate (only a selection change clears it), so a rule filtered on `EditorSelected` but not `EditorState` — such as the gizmo renderers — has no other way to detect "editor is off" and stop drawing.
- `template_names() list[string]` — returns the registered `pub template` names in declaration order, per `editor-template-registry`.
- `template_index(name: string) int` — returns `name`'s zero-based position in the same order `template_names()` returns (or `-1` if unregistered), per `editor-template-registry`. Used to obtain a per-item index for layout/tint without relying on loop-local mutation inside `for`, which bounded `foreach` does not support (see `editor-declarative-rendering` design notes).
- `screen_size() vec2` — returns the current window/render-target size in screen pixels.
- `palette_label_slot(index: int) entity_id` — returns the entity handle for the `index`'th pre-spawned palette-button label slot from a small fixed pool (or an invalid `entity_id` past the pool size), so `EditorTemplatePalette` has a stable per-index entity to `project ScreenLabel to` each frame. `ScreenLabel` is entity-attached, not an event, and there is no DSL mechanism to spawn or discover entities indexed by an arbitrary per-frame count (see `editor-declarative-rendering` design notes, decision 9).
- `palette_color(index: int) color` — cycles a small fixed 6-color palette by `index % 6`, for index-based palette button tinting. Backend-computed rather than a DSL if-chain assigning into a `let`-bound local, since `VarAssign` to a plain local inside a nested `if`/`for` block always redeclares with `auto` instead of mutating the outer binding (the same root-cause codegen limitation as the `foreach`-mutation finding in `editor-declarative-rendering`'s design notes, confirmed also reachable via `if`).
- `mode_label(mode: GizmoMode) string` — returns the `GizmoMode` name (`SELECT`/`TRANSLATE`/`ROTATE`/`SCALE`/`PLACE`) for the HUD overlay's mode text. Same backend-accessor rationale as `palette_color`.
- `palette_button_y(index: int) float` — returns the Y screen position (pixels) for the `index`'th palette button (`40 + index * 30`, matching the 140×26px buttons with a 4px gap). Backend-computed because the DSL has no explicit int-to-float cast, so `40.0 + (idx * 30.0)` promotes `idx` implicitly, which this project's `clang-tidy` narrowing-conversion check (run with `--warnings-as-errors=*` on every curated example) flags.

#### Scenario: editor_spawn_template declared with correct signature
- **WHEN** `std.editor` is imported
- **THEN** `editor_spawn_template` is callable with `(string, vec2, vec3)` arguments and returns `entity_id`

#### Scenario: editor_hit_test_2d declared with correct signature
- **WHEN** `std.editor` is imported
- **THEN** `editor_hit_test_2d` is callable with `(vec2, int)` arguments and returns `entity_id`

#### Scenario: editor_raycast_3d declared with correct signature
- **WHEN** `std.editor` is imported
- **THEN** `editor_raycast_3d` is callable with `(vec2, int)` arguments and returns `entity_id`

#### Scenario: active_mode reflects the live EditorState.mode
- **WHEN** `EditorState.mode` is `GizmoMode.Rotate` on the singleton `Editor` entity
- **THEN** `active_mode()` called from any rule returns `GizmoMode.Rotate`

#### Scenario: mode_label accepts a GizmoMode argument
- **WHEN** a rule calls `mode_label(GizmoMode.Scale)`
- **THEN** the call type-checks against a `GizmoMode` parameter and returns `"SCALE"`

#### Scenario: is_editor_active reflects the live EditorState.active
- **WHEN** `EditorState.active` is `false` on the singleton `Editor` entity
- **THEN** `is_editor_active()` called from any rule returns `false`, even if `EditorSelected` is still present on an entity from a prior edit session

#### Scenario: template_names is callable and returns a list
- **WHEN** `std.editor` is imported and at least one `pub template` is declared
- **THEN** `template_names()` is callable with no arguments and returns `list[string]`

#### Scenario: template_index returns the declaration-order position
- **WHEN** `std.editor` is imported and templates `"Box"`, `"PlayerSpawn"` are declared in that order
- **THEN** `template_index("PlayerSpawn")` returns `1`

#### Scenario: screen_size reflects the current window size
- **WHEN** the render target is 1280x720 pixels
- **THEN** `screen_size()` returns `{1280.0, 720.0}`

### Requirement: std.editor module exposes extern rule declarations
The `std.editor` module SHALL declare `pub extern rule EditorPropertyPanel` with filter requiring `EditorState`. `EditorPropertyPanel` remains an unimplemented (no-op) compiler-provided stub.

`EditorTemplatePalette`, `GizmoRenderer2D`, and `GizmoRenderer3D` are no longer declared as `extern rule`; they are ordinary `pub rule` (or `rule`) declarations with DSL-authored bodies, specified under the requirements below and in `editor-debug-draw`/`editor-screen-ui`.

#### Scenario: EditorPropertyPanel extern rule is declared with correct filter
- **WHEN** `std.editor` is imported
- **THEN** `EditorPropertyPanel` is an available extern rule that filters on entities with `EditorState`

#### Scenario: GizmoRenderer2D is not a compiler-implemented extern rule
- **WHEN** `std.editor` is imported
- **THEN** `GizmoRenderer2D` resolves to a plain DSL rule defined in `std.editor`, not a compiler-provided extern rule implementation

### Requirement: std.editor module provides EditorModeToggle rule
The `std.editor` module SHALL declare a `rule EditorModeToggle` that filters on `EditorState as state` and handles keyboard shortcuts:
- `on input:` with `input.pressed(Key.F1)` toggles `state.active`
- `on input:` with `input.pressed(Key.W)` sets `state.mode = GizmoMode.Translate`
- `on input:` with `input.pressed(Key.E)` sets `state.mode = GizmoMode.Rotate`
- `on input:` with `input.pressed(Key.R)` sets `state.mode = GizmoMode.Scale`
- `on input:` with `input.pressed(Key.T)` sets `state.mode = GizmoMode.Place`

#### Scenario: F1 toggles editor active state
- **WHEN** `EditorState.active` is `true` and `input.pressed(Key.F1)` is called
- **THEN** `EditorState.active` becomes `false`
- **WHEN** `EditorState.active` is `false` and `input.pressed(Key.F1)` is called again
- **THEN** `EditorState.active` becomes `true`

#### Scenario: W key sets Translate mode
- **WHEN** `input.pressed(Key.W)` is called
- **THEN** `EditorState.mode` is set to `GizmoMode.Translate`

### Requirement: std.editor module provides 2D selection and transform rules
The `std.editor` module SHALL declare:
- `rule EditorSelection2D` with filter requiring `std.transform.flat.WorldTransform` and `BoxCollider`, excluding `EditorLocked`, that on input click calls `editor_hit_test_2d` and adds `EditorSelected` to the hit entity
- `rule EditorTranslate2D` with filter requiring `std.transform.flat.WorldTransform as xform` and `EditorSelected`, that on tick while mouse is held adds `editor_mouse_delta_2d()` to `xform.position`
- `rule EditorPlace2D` with filter requiring `EditorState as state`, that on input click in `GizmoMode.Place` mode calls `editor_spawn_template` with `state.active_template` and the screen-to-world position, then adds `EditorSelected` to the spawned entity
- `rule EditorGizmoRenderer2D` with filter requiring `std.transform.flat.WorldTransform` and `EditorSelected`, that on render reads `active_mode()`, projects `EditorGizmo2D` with that mode, color `#00FF00FF`, and size `1.0`, and emits the `std.debug`/`std.ui` primitive events (per `editor-debug-draw`) corresponding to that mode's gizmo geometry: an always-drawn AABB outline, plus mode-specific axis lines (Translate), a ring (Rotate), or axis lines with tip markers (Scale). It uses `on render:`, not `on tick:`, so its emitted events are drained within the render-frame flush boundary (see `editor-visual-runtime`), using that frame's camera rather than a stale one from the previous frame.

`EditorSelection2D` and `EditorPlace2D` SHALL act only when `EditorState.use_3d` is `false`; when it is `true` their input handlers SHALL perform no selection and no spawning.

#### Scenario: Click selects a 2D entity
- **WHEN** editor is active, `use_3d` is `false`, and user clicks on a flat-world entity with `BoxCollider`
- **THEN** the entity receives `EditorSelected` component

#### Scenario: Drag translates a 2D entity
- **WHEN** an entity has `EditorSelected` and `std.transform.flat.WorldTransform`
- **AND** mouse is held
- **THEN** the entity's `WorldTransform.position` changes by the mouse delta each frame

#### Scenario: Click in Place mode spawns a template entity
- **WHEN** `EditorState.mode` is `GizmoMode.Place`, `EditorState.use_3d` is `false`, and `EditorState.active_template` is `"Tree"`
- **AND** user clicks in the world
- **THEN** a new entity is created from the `Tree` template at the clicked position
- **AND** the new entity receives `EditorSelected` component

#### Scenario: 2D placement is inert when use_3d is true
- **WHEN** `EditorState.mode` is `GizmoMode.Place` and `EditorState.use_3d` is `true`
- **AND** user clicks in the world
- **THEN** `EditorPlace2D` spawns no entity

#### Scenario: Gizmo mode follows EditorState.mode
- **WHEN** an entity has `EditorSelected` and `std.transform.flat.WorldTransform`, and `EditorState.mode` is `GizmoMode.Rotate`
- **THEN** `EditorGizmoRenderer2D` projects `EditorGizmo2D` with `mode = GizmoMode.Rotate` and emits a `DrawDebugRingOutline2D` event that frame, not translate arrows

#### Scenario: Gizmo AABB outline always drawn regardless of mode
- **WHEN** an entity has `EditorSelected` and `std.transform.flat.WorldTransform`
- **THEN** `EditorGizmoRenderer2D` emits a `DrawDebugRectOutline2D` event around the entity's AABB every render frame, in addition to any mode-specific geometry

### Requirement: std.editor module provides 3D selection and transform rules
The `std.editor` module SHALL declare:
- `rule EditorSelection3D` with filter requiring `std.transform.volume.WorldTransform`, excluding `EditorLocked`, that on input click calls `editor_raycast_3d` and adds `EditorSelected` to the hit entity
- `rule EditorTranslate3D` with filter requiring `std.transform.volume.WorldTransform as xform` and `EditorSelected`, that on tick while mouse is held adds `editor_mouse_delta_3d()` to `xform.position`
- `rule EditorPlace3D` with filter requiring `EditorState as state`, that on input click in `GizmoMode.Place` mode calls `editor_spawn_template` with `state.active_template` and the position returned by `editor_plane_project_3d` against the ground plane (origin `(0,0,0)`, normal `(0,1,0)`), then adds `EditorSelected` to the spawned entity
- `rule EditorGizmoRenderer3D` with filter requiring `std.transform.volume.WorldTransform` and `EditorSelected`, that on render reads `active_mode()`, projects `EditorGizmo3D` with that mode, color `#00FF00FF`, and size `1.0`, and emits the `std.debug` primitive events (per `editor-debug-draw`) corresponding to that mode's gizmo geometry: an always-drawn wire box, plus mode-specific axis lines (Translate), a circle (Rotate), or axis lines with tip markers (Scale). Like `EditorGizmoRenderer2D`, it uses `on render:` so its emitted events are drained within the render-frame flush boundary using that frame's camera.

`EditorSelection3D` and `EditorPlace3D` SHALL act only when `EditorState.use_3d` is `true`; when it is `false` their input handlers SHALL perform no selection and no spawning.

#### Scenario: Click selects a 3D entity
- **WHEN** editor is active, `use_3d` is `true`, and user clicks on a volume-world entity
- **THEN** the entity receives `EditorSelected` component

#### Scenario: Click in Place mode spawns a template at the ground-plane hit point
- **WHEN** `EditorState.mode` is `GizmoMode.Place`, `EditorState.use_3d` is `true`, and `EditorState.active_template` names a registered template
- **AND** user clicks in the world
- **THEN** a new entity is created from the template at the point where the cursor ray intersects the y=0 plane
- **AND** the new entity receives `EditorSelected` component

#### Scenario: 3D placement is inert when use_3d is false
- **WHEN** `EditorState.mode` is `GizmoMode.Place` and `EditorState.use_3d` is `false`
- **AND** user clicks in the world
- **THEN** `EditorPlace3D` spawns no entity

#### Scenario: Drag translates a 3D entity
- **WHEN** an entity has `EditorSelected` and `std.transform.volume.WorldTransform`
- **AND** mouse is held
- **THEN** the entity's `WorldTransform.position` changes by the mouse delta each frame

#### Scenario: Gizmo mode follows EditorState.mode
- **WHEN** an entity has `EditorSelected` and `std.transform.volume.WorldTransform`, and `EditorState.mode` is `GizmoMode.Scale`
- **THEN** `EditorGizmoRenderer3D` projects `EditorGizmo3D` with `mode = GizmoMode.Scale` and emits axis-line and tip-marker (`DrawDebugCube3D`) events that frame, not a rotate circle

#### Scenario: Gizmo wire box always drawn regardless of mode
- **WHEN** an entity has `EditorSelected` and `std.transform.volume.WorldTransform`
- **THEN** `EditorGizmoRenderer3D` emits a `DrawDebugWireBox3D` event around the entity every render frame, in addition to any mode-specific geometry

### Requirement: std.editor module provides a DSL-authored template palette rule
The `std.editor` module SHALL declare a plain `rule EditorTemplatePalette` filtered on `EditorState`, active only when `EditorState.active` is `true`, that on each render frame:
1. Calls `template_names()` to get the registered template list.
2. For each name, computes a screen position (starting at `(10, 40)`, each button `140x26` pixels, `4px` gap, stacked vertically by index) and a color chosen by cycling a small fixed palette indexed by that template's position in the list.
3. Emits a `DrawScreenRect` event (filled) for the button background and projects/uses `ScreenLabel` for the template name text.
4. When the left mouse button is pressed and the cursor is within a button's screen rectangle, sets `EditorState.active_template` to that template's name and `EditorState.mode` to `GizmoMode.Place`.

Click hit-testing and the `EditorState` writes SHALL be performed by this rule directly (it has `self` bound to the `EditorState` entity), not by the generic `DrawScreenRect` renderer.

#### Scenario: Palette visible in edit mode
- **WHEN** `EditorState.active` is `true` and templates `"Box"` and `"PlayerSpawn"` are registered
- **THEN** two `DrawScreenRect` events are emitted that frame, at `(10, 40)` and `(10, 70)`, with different palette colors

#### Scenario: Palette hidden outside edit mode
- **WHEN** `EditorState.active` is `false`
- **THEN** `EditorTemplatePalette` emits no `DrawScreenRect` events that frame, and updates every label's `ScreenLabel.visible` to `false`

Explicit hide, not merely "no update": a button label's `ScreenLabel` is a persistent pool-slot entity (see the requirement above), so if the rule simply stopped updating it on deactivate, it would keep showing whatever it last displayed while active — this scenario was revised from "emits no ... `ScreenLabel` updates" to match `EditorHUDOverlay`'s already-correct "update to `visible = false`" behavior below.

#### Scenario: Clicking a template button
- **WHEN** `EditorState.active` is `true` and the user clicks within the `"Box"` button's screen rectangle
- **THEN** `EditorState.active_template` is set to `"Box"` and `EditorState.mode` is set to `GizmoMode.Place`

#### Scenario: Button color depends on registration order
- **WHEN** two templates are registered in a given declaration order
- **THEN** their button colors are assigned deterministically by that order (index-based), not derived from the template name itself

### Requirement: std.editor module provides a DSL-authored edit-mode HUD overlay rule
The `std.editor` module SHALL declare a plain `rule EditorHUDOverlay` filtered on `EditorState`, that on each render frame, when `EditorState.active` is `true`:
1. Emits a `DrawScreenRect` event (outline, `3px` thickness, `YELLOW`) spanning the full screen bounds (using `screen_size()`).
2. Maintains a `ScreenLabel` at screen position `(10, 10)` in yellow, with text built via `text.format` showing the current mode: `"EDIT [<MODE>]  F1:toggle  W:trans  E:rot  R:scale  T:place"`, where `<MODE>` is one of `SELECT` / `TRANSLATE` / `ROTATE` / `SCALE` / `PLACE` based on `EditorState.mode`.

When `EditorState.active` is `false`, the rule SHALL draw no border and update the HUD `ScreenLabel` to `visible = false`.

#### Scenario: Overlay visible in edit mode
- **WHEN** `EditorState.active` is `true` and `EditorState.mode` is `GizmoMode.Translate`
- **THEN** a `DrawScreenRect` outline event spanning the screen and a `ScreenLabel` reading `"EDIT [TRANSLATE]  F1:toggle  W:trans  E:rot  R:scale  T:place"` are produced that frame

#### Scenario: Overlay hidden outside edit mode
- **WHEN** `EditorState.active` is `false`
- **THEN** no border `DrawScreenRect` event is emitted, and the HUD `ScreenLabel` has `visible = false`

#### Scenario: Overlay is a documented, DSL-visible rule
- **WHEN** a Cactus program imports `std.editor`
- **THEN** `EditorHUDOverlay` appears as an ordinary rule in the merged program, orderable via `after:`/`before:` like any other rule — unlike the prior compiler-spliced overlay, which had no DSL-visible identity
