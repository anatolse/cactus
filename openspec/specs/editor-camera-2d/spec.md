## Purpose
Define the runtime active camera state API for the 2D editor, including frame-local camera state management, codegen-emitted camera-sync blocks, and symmetric 3D camera stubs.

## Requirements

### Requirement: Runtime maintains a per-frame active 2D camera state
The backend runtime SHALL expose `set_active_camera_2d(Camera2D)` and `get_active_camera_2d()` functions. The active camera state SHALL be a frame-local global, initialized to an identity Camera2D (zoom=1.0, zero offset and target) at startup. Any code within the same frame that reads `get_active_camera_2d()` SHALL see the value last written by `set_active_camera_2d`.

#### Scenario: Default camera is identity
- **WHEN** no Camera entity exists in the registry and no code calls `set_active_camera_2d`
- **THEN** `get_active_camera_2d()` returns a Camera2D with zoom=1.0, offset=(0,0), target=(0,0), rotation=0

#### Scenario: Camera sync from entity
- **WHEN** a Camera entity with `active=true`, `zoom=64.0`, and `offset=vec2(0,0)` exists
- **THEN** after the camera-sync codegen block runs, `get_active_camera_2d()` returns a Camera2D with zoom=64.0

#### Scenario: Only the first active camera is used
- **WHEN** multiple Camera entities exist with `active=true`
- **THEN** `set_active_camera_2d` is called exactly once (for the first active camera found)

### Requirement: Codegen emits camera-sync block at top of generated_update_project
When a module imports `std.camera.flat`, the codegen SHALL emit a camera-sync block as the first statement of `generated_update_project`. The block SHALL iterate `registry.view<Camera>()` and call `set_active_camera_2d` with a `Camera2D` derived from the first entity where `Camera.active == true`. The Camera2D SHALL set `target` from `Camera.offset`, `zoom` from `Camera.zoom`, `rotation` from `Camera.rotation` (converted degrees to radians), and `offset` to `{screenWidth/2, screenHeight/2}` (world origin maps to screen center).

#### Scenario: Camera sync runs before editor selection
- **WHEN** a module uses both `std.camera.flat` and `std.editor`
- **THEN** the camera-sync block appears before any `editor_*_input` call in `generated_update_project`

#### Scenario: No camera sync emitted without import
- **WHEN** a module does not import `std.camera.flat`
- **THEN** no camera-sync block is emitted and the active camera remains at its identity default

### Requirement: Runtime provides symmetric 3D camera state stubs
The runtime SHALL expose `set_active_camera_3d(Camera3D)` and `get_active_camera_3d()` functions for future 3D editor symmetry. The default Camera3D SHALL match the hardcoded camera previously used in `flush_mesh_queue` (position=(6,6,6), target=(0,0,0), up=(0,1,0), fovy=45). `flush_mesh_queue` SHALL use `get_active_camera_3d()` instead of a hardcoded literal.

#### Scenario: flush_mesh_queue uses active 3D camera
- **WHEN** no Camera3D entity is registered (no 3D camera module in use)
- **THEN** `flush_mesh_queue` uses the default Camera3D (position=(6,6,6)) — same behavior as before
