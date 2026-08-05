# Spec: editor-screen-ui

## Purpose
Define the `std.ui` stdlib module, which provides screen-space draw primitives usable by any Cactus program (not only the in-game editor), starting with a filled/outline rectangle primitive and its single generic backend renderer.

## Requirements

### Requirement: std.ui module provides a screen-space rectangle draw primitive
The stdlib SHALL declare a `std.ui` module independent of `std.editor`, exposing `pub event DrawScreenRect` with fields `position: vec2` (screen pixels, top-left origin), `size: vec2` (screen pixels), `color: color`, `filled: bool`, and `thickness: float` (used only when `filled` is `false`). This event SHALL be usable by any Cactus program, not only `std.editor`. Screen-space text reuses the existing `std.render.text.ScreenLabel` trait; no new text primitive is introduced.

#### Scenario: Filled rect event is declared with correct fields
- **WHEN** `std.ui` is imported
- **THEN** `DrawScreenRect` is a valid event with `position`, `size`, `color`, `filled`, and `thickness` fields

#### Scenario: Non-editor code can draw screen-space rects
- **WHEN** a program that does not import `std.editor` imports `std.ui` and emits `DrawScreenRect` with `filled = true`
- **THEN** a filled rectangle is drawn at the given screen position and size that frame

### Requirement: DrawScreenRect has exactly one generic backend renderer
`DrawScreenRect` SHALL have exactly one compiler-implemented handler rule that runs in the render phase, reads only the event's payload fields, and issues the corresponding raylib call (`DrawRectangleRec` when `filled` is `true`, `DrawRectangleLinesEx` with `thickness` when `filled` is `false`). This handler SHALL contain no editor-specific or entity-filter-specific branching.

#### Scenario: Filled rect renders as a solid rectangle
- **WHEN** a rule emits `DrawScreenRect: position = {10, 40}, size = {140, 26}, color = BLUE, filled = true`
- **THEN** a solid blue rectangle is drawn at screen position (10, 40) sized 140x26 pixels that frame

#### Scenario: Outline rect renders as a bordered rectangle
- **WHEN** a rule emits `DrawScreenRect: position = {0, 0}, size = {screenW, screenH}, color = YELLOW, filled = false, thickness = 3.0`
- **THEN** a 3px yellow rectangle outline is drawn along the given bounds that frame

#### Scenario: Multiple rects in one frame all render independently
- **WHEN** a rule emits `DrawScreenRect` multiple times in the same frame with different positions
- **THEN** each rectangle is drawn independently; none overwrite or coalesce with another
