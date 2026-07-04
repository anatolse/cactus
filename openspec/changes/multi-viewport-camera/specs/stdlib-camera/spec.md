## MODIFIED Requirements

### Requirement: std.camera.flat provides 2D camera traits
The `std.camera.flat` module SHALL provide a `Camera` trait for 2D orthographic rendering. A `FollowCamera` trait is provided for camera-entity tracking; the associated `FollowCameraSystem` updates the `Camera` offset on `late_tick`. Active camera selection is governed by `std.camera.viewport.Viewport` — the `active` field has been removed from `Camera`.

#### Scenario: Camera trait fields for 2D
- **WHEN** `use std.camera.flat as cam` is imported and an entity has `cam.Camera`
- **THEN** the entity has fields: `zoom: float = 1.0`, `offset: vec2 = vec2(0,0)`, `rotation: float = 0.0`
- **AND** the entity does NOT have an `active: bool` field

#### Scenario: FollowCamera drives camera offset
- **WHEN** an entity has both `cam.Camera` and `cam.FollowCamera` with a valid `target: entity_id`
- **THEN** `FollowCameraSystem` updates `Camera.offset` each `late_tick` to track the target's position with smoothing

---

### Requirement: std.camera.volume provides 3D camera traits and helper systems
The `std.camera.volume` module SHALL provide a `Camera` trait for 3D perspective rendering. Additional traits define common camera behaviors: `FollowCamera` (third-person follow), `FirstPersonCamera` (FPS look), and `ThirdPersonCamera` (orbit camera). Active systems on `late_tick` update the entity's volume Transform based on these traits. Active camera selection is governed by `std.camera.viewport.Viewport` — the `active` field has been removed from `Camera`.

#### Scenario: Camera trait fields for 3D
- **WHEN** `use std.camera.volume as cam` is imported and an entity has `cam.Camera`
- **THEN** the entity has fields: `fov_y: float = 60.0`, `near: float = 0.1`, `far: float = 1000.0`
- **AND** the entity does NOT have an `active: bool` field

#### Scenario: FirstPersonCamera responds to look input
- **WHEN** a `FirstPersonCamera` entity's `pitch` and `yaw` fields are updated by a user input system
- **THEN** `FirstPersonCameraSystem` updates the entity's volume Transform rotation on `late_tick`

#### Scenario: ThirdPersonCamera orbits around target
- **WHEN** a `ThirdPersonCamera` entity has a valid `target: entity_id` with a volume Transform
- **THEN** `ThirdPersonCameraSystem` positions the camera at `target.position + offset(distance, height)` on `late_tick`

---

## REMOVED Requirements

### Requirement: Only one active camera is used
**Reason**: The `active: bool` field on `Camera` is removed. Multiple viewports are now supported through `std.camera.viewport.Viewport`. Each Viewport entity selects its own camera by composition; there is no single "active" boolean anymore.
**Migration**: Remove `Camera.active` field access from user code. Add a `Viewport` trait to any entity that should render. `Viewport.active` controls whether a viewport renders each frame.

### Requirement: std.camera.flat.Camera entity drives the runtime active camera
**Reason**: Active-camera driving is now handled by the codegen-emitted viewport render loop (see `stdlib-viewport`). The camera-sync block in `generated_update_project` is only emitted on the legacy path (no `std.camera.viewport` import).
**Migration**: Import `std.camera.viewport` and add `Viewport` to camera entities. The viewport loop handles `set_active_camera_2d` automatically.
