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

#### Scenario: editor_spawn_template declared with correct signature
- **WHEN** `std.editor` is imported
- **THEN** `editor_spawn_template` is callable with `(string, vec2, vec3)` arguments and returns `entity_id`

#### Scenario: editor_hit_test_2d declared with correct signature
- **WHEN** `std.editor` is imported
- **THEN** `editor_hit_test_2d` is callable with `(vec2, int)` arguments and returns `entity_id`

#### Scenario: editor_raycast_3d declared with correct signature
- **WHEN** `std.editor` is imported
- **THEN** `editor_raycast_3d` is callable with `(vec2, int)` arguments and returns `entity_id`

### Requirement: std.editor module exposes extern rule declarations
The `std.editor` module SHALL declare the following `pub extern rule` declarations:
- `EditorTemplatePalette` with filter requiring `EditorState`
- `EditorPropertyPanel` with filter requiring `EditorState`
- `GizmoRenderer2D` with filter requiring `EditorGizmo2D` and `std.transform.flat.WorldTransform`
- `GizmoRenderer3D` with filter requiring `EditorGizmo3D` and `std.transform.volume.WorldTransform`

#### Scenario: EditorTemplatePalette extern rule is declared with correct filter
- **WHEN** `std.editor` is imported
- **THEN** `EditorTemplatePalette` is an available extern rule that filters on entities with `EditorState`

#### Scenario: GizmoRenderer2D extern rule is declared with correct filter
- **WHEN** `std.editor` is imported
- **THEN** `GizmoRenderer2D` is an available extern rule that filters on entities with both `EditorGizmo2D` and `std.transform.flat.WorldTransform`

#### Scenario: GizmoRenderer3D extern rule is declared with correct filter
- **WHEN** `std.editor` is imported
- **THEN** `GizmoRenderer3D` is an available extern rule that filters on entities with both `EditorGizmo3D` and `std.transform.volume.WorldTransform`

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
- `rule EditorGizmo2D` with filter requiring `std.transform.flat.WorldTransform` and `EditorSelected`, that on tick projects `EditorGizmo2D` with the current mode, color `#00FF00FF`, and size `1.0`

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

#### Scenario: Gizmo is projected on selected 2D entities each frame
- **WHEN** an entity has `EditorSelected` and `std.transform.flat.WorldTransform`
- **THEN** `EditorGizmo2D` is projected onto the entity each tick

### Requirement: std.editor module provides 3D selection and transform rules
The `std.editor` module SHALL declare:
- `rule EditorSelection3D` with filter requiring `std.transform.volume.WorldTransform`, excluding `EditorLocked`, that on input click calls `editor_raycast_3d` and adds `EditorSelected` to the hit entity
- `rule EditorTranslate3D` with filter requiring `std.transform.volume.WorldTransform as xform` and `EditorSelected`, that on tick while mouse is held adds `editor_mouse_delta_3d()` to `xform.position`
- `rule EditorPlace3D` with filter requiring `EditorState as state`, that on input click in `GizmoMode.Place` mode calls `editor_spawn_template` with `state.active_template` and the position returned by `editor_plane_project_3d` against the ground plane (origin `(0,0,0)`, normal `(0,1,0)`), then adds `EditorSelected` to the spawned entity
- `rule EditorGizmo3D` with filter requiring `std.transform.volume.WorldTransform` and `EditorSelected`, that on tick projects `EditorGizmo3D` with the current mode, color `#00FF00FF`, and size `1.0`

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

#### Scenario: Gizmo is projected on selected 3D entities each frame
- **WHEN** an entity has `EditorSelected` and `std.transform.volume.WorldTransform`
- **THEN** `EditorGizmo3D` is projected onto the entity each tick
