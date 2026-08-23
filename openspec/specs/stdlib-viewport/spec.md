## Purpose

Define the `std.camera.viewport` stdlib module's `Viewport` trait and the per-viewport render-loop code generation it triggers when imported.

## Requirements

### Requirement: std.camera.viewport provides the Viewport trait
The `std.camera.viewport` module SHALL provide a `Viewport` trait that defines a normalized screen-space region for rendering. Viewport rects are in 0.0–1.0 space (resolution-independent). An entity with both `Viewport` and a Camera trait (`std.camera.flat.Camera` or `std.camera.volume.Camera`) is an active rendering viewport.

#### Scenario: Viewport trait fields
- **WHEN** `use std.camera.viewport as vp` is imported and an entity has `vp.Viewport`
- **THEN** the entity has fields: `x: float = 0.0`, `y: float = 0.0`, `width: float = 1.0`, `height: float = 1.0`, `depth: int = 0`, `clear: bool = true`, `clear_color: color = #000000FF`, `active: bool = true`

#### Scenario: Default Viewport covers full screen
- **WHEN** a `Viewport` entity has all default field values
- **THEN** the viewport covers the full screen (0,0) to (screenWidth, screenHeight)

#### Scenario: Mini-map viewport in top-right corner
- **WHEN** a `Viewport` entity has `x=0.75`, `y=0.0`, `width=0.25`, `height=0.25`
- **THEN** the viewport covers the top-right quarter of the screen

---

### Requirement: Codegen emits a per-viewport render loop when std.camera.viewport is imported
When a module imports `std.camera.viewport`, the cpp-entt codegen SHALL emit a viewport render loop within the render-frame flush boundary (the code path between `begin_render_frame` and `end_render_frame`) — inside `generated_render_project` for the legacy main loop, or inside the render phase activation's dispatch for the graph-driven main loop. The loop SHALL replace the single camera-sync block in `generated_update_project` for modules using the legacy main loop.

#### Scenario: Viewports rendered in depth order
- **WHEN** two Viewport entities exist with `depth=0` and `depth=10`
- **THEN** the viewport with `depth=0` is rendered first (underneath the depth=10 viewport)

#### Scenario: Inactive viewport is skipped
- **WHEN** a `Viewport` entity has `active = false`
- **THEN** that entity is not rendered in the viewport loop

#### Scenario: Per-viewport scissor clip
- **WHEN** a Viewport has `x=0.0`, `y=0.0`, `width=0.5`, `height=1.0` (left half of screen)
- **THEN** all draw calls for that viewport are scissor-clipped to the left half of the screen

#### Scenario: Per-viewport clear
- **WHEN** a Viewport has `clear = true` and `clear_color = #FF0000FF`
- **THEN** the viewport region is cleared to red before rendering its contents

#### Scenario: Viewport with clear=false does not clear
- **WHEN** a Viewport has `clear = false`
- **THEN** the viewport region is not cleared; previously drawn content is visible beneath

#### Scenario: 2D camera set per viewport
- **WHEN** a Viewport entity also has `std.camera.flat.Camera` with `zoom=32`
- **THEN** the render loop calls `set_active_camera_2d` with a Camera2D derived from that entity's Camera before calling render rules for that viewport

#### Scenario: 3D camera set per viewport
- **WHEN** a Viewport entity also has `std.camera.volume.Camera` and `std.transform.volume.Transform`
- **THEN** the render loop calls `set_active_camera_3d` with a Camera3D derived from that entity's Camera and Transform before calling render rules for that viewport

#### Scenario: Viewport without Camera trait has no view transform set
- **WHEN** a Viewport entity has neither `std.camera.flat.Camera` nor `std.camera.volume.Camera`
- **THEN** the render loop does not call `set_active_camera_2d` or `set_active_camera_3d` for that viewport (active camera remains at its previously set value)

#### Scenario: Viewport loop executes under the graph-driven main loop
- **WHEN** a program importing `std.camera.viewport` is generated with a non-empty execution graph and a resolved external frame event (graph-driven main loop)
- **THEN** the viewport render loop still executes once per frame, inside the render phase activation's dispatch, between `begin_render_frame` and `end_render_frame`
