# Spec: editor-debug-draw

## Purpose
Define the `std.debug` stdlib module, which provides world-space 2D and 3D one-shot debug-draw primitive events usable by any Cactus program (not only the in-game editor), their generic backend renderers, and the dedicated 3D edit-mode ground-grid extern rule.

## Requirements

### Requirement: std.debug module provides world-space 2D debug-draw primitive events
The stdlib SHALL declare a `std.debug` module independent of `std.editor`, exposing `pub event` declarations for one-shot, world-space 2D vector debug-draw primitives: `DrawDebugLine2D` (fields `start: vec2`, `end: vec2`, `color: color`, `thickness: float`), `DrawDebugTriangle2D` (fields `a: vec2`, `b: vec2`, `c: vec2`, `color: color`), `DrawDebugRingOutline2D` (fields `center: vec2`, `inner_radius: float`, `outer_radius: float`, `color: color`), and `DrawDebugRectOutline2D` (fields `position: vec2`, `size: vec2`, `thickness: float`, `color: color`). These events SHALL be usable by any Cactus program, not only `std.editor`.

`from`/`to` were the field names originally proposed for `DrawDebugLine2D`/`3D`, but both are reserved DSL keywords (used by `project ... to ...` and `remove ... from ...`) and cannot be used as struct/event field names — confirmed by attempting to compile a minimal fixture. `start`/`end` are used instead; `end` is not a reserved word in this grammar.

#### Scenario: Debug line event is declared with correct fields
- **WHEN** `std.debug` is imported
- **THEN** `DrawDebugLine2D` is a valid event with `start`, `end`, `color`, and `thickness` fields

#### Scenario: Non-editor code can emit debug-draw events
- **WHEN** a program that does not import `std.editor` imports `std.debug` and emits `DrawDebugLine2D`
- **THEN** the emit is accepted and the line is drawn in the render phase

### Requirement: std.debug module provides world-space 3D debug-draw primitive events
The `std.debug` module SHALL declare `pub event` declarations for one-shot, world-space 3D vector debug-draw primitives: `DrawDebugLine3D` (fields `start: vec3`, `end: vec3`, `color: color`), `DrawDebugWireBox3D` (fields `center: vec3`, `size: vec3`, `color: color`), `DrawDebugCircle3D` (fields `center: vec3`, `radius: float`, `normal: vec3`, `color: color`), and `DrawDebugCube3D` (fields `center: vec3`, `size: vec3`, `color: color`).

#### Scenario: Wire box event is declared with correct fields
- **WHEN** `std.debug` is imported
- **THEN** `DrawDebugWireBox3D` is a valid event with `center`, `size`, and `color` fields

### Requirement: Each debug-draw event has exactly one generic backend renderer
Each event declared by `std.debug` SHALL have exactly one compiler-implemented handler rule that runs in the render phase, reads only that event's payload fields, and issues the corresponding raylib draw call. These handler rules SHALL contain no mode-specific, editor-specific, or entity-filter-specific branching — they are generic pass-through renderers, structurally equivalent to `std.render.text`'s `ScreenLabelRender`.

#### Scenario: Emitting a debug line draws it once
- **WHEN** a rule executes `emit DrawDebugLine3D: start = a, end = b, color = RED` once during a frame
- **THEN** exactly one line segment from `a` to `b` in `RED` is drawn that frame

#### Scenario: Emitting the same event multiple times in one frame draws multiple primitives
- **WHEN** a rule executes `emit DrawDebugLine3D:` three times in the same frame with different `start`/`end` values
- **THEN** three distinct line segments are drawn that frame, none overwriting or coalescing with another

#### Scenario: No events emitted draws nothing
- **WHEN** no `std.debug` event is emitted during a frame
- **THEN** the corresponding renderer draws nothing that frame

### Requirement: 3D edit-mode ground grid remains a dedicated generic extern rule
The always-on edit-mode ground grid (`DrawGrid(20, 1.0)`) SHALL be implemented as a small, dedicated, compiler-owned extern rule filtered on the existence of an `EditorCamera3D` entity (the existing edit-mode rig-existence gate used elsewhere in `std.editor`), independent of any specific gizmo or debug-draw event. It SHALL require no per-entity data.

#### Scenario: Grid renders whenever the 3D edit rig exists
- **WHEN** an `EditorCamera3D` entity exists
- **THEN** the render phase draws the y=0 ground grid, regardless of whether any `DrawDebugWireBox3D` or other debug-draw event was emitted that frame

#### Scenario: No grid without an active 3D rig
- **WHEN** no `EditorCamera3D` entity exists
- **THEN** no ground grid is drawn
