## Purpose

Define the `std.camera` stdlib module's 2D (`flat`) and 3D (`volume`) camera traits and helper rules, including current stubs pending an entity query mechanism.

## Requirements

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

### Requirement: std.camera.volume provides 3D camera traits and helper rules
The `std.camera.volume` module SHALL provide a `Camera` trait for 3D perspective rendering. Additional traits define common camera behaviors: `FollowCamera` (third-person follow), `FirstPersonCamera` (FPS look), and `ThirdPersonCamera` (orbit camera). Active rules on `late_tick` update the entity's volume Transform based on these traits. Active camera selection is governed by `std.camera.viewport.Viewport` — the `active` field has been removed from `Camera`.

#### Scenario: Camera trait fields for 3D
- **WHEN** `use std.camera.volume as cam` is imported and an entity has `cam.Camera`
- **THEN** the entity has fields: `fov_y: float = 60.0`, `near: float = 0.1`, `far: float = 1000.0`
- **AND** the entity does NOT have an `active: bool` field

#### Scenario: FirstPersonCamera responds to look input
- **WHEN** a `FirstPersonCamera` entity's `pitch` and `yaw` fields are updated by a user input rule
- **THEN** `FirstPersonCameraSystem` updates the entity's volume Transform rotation on `late_tick`

#### Scenario: ThirdPersonCamera orbits around target
- **WHEN** a `ThirdPersonCamera` entity has a valid `target: entity_id` with a volume Transform
- **THEN** `ThirdPersonCameraSystem` positions the camera at `target.position + offset(distance, height)` on `late_tick`

---

### Requirement: Camera rules are stubbed pending entity query mechanism
The active camera rules (`FollowCameraSystem`, `FirstPersonCameraSystem`, `ThirdPersonCameraSystem`) SHALL be declared in the module files but their implementation bodies SHALL be stubbed with a `# TODO: requires entity query` comment. The trait definitions SHALL be complete and usable. Users may implement camera logic manually in the interim using the camera traits directly.

#### Scenario: Camera traits usable before rules are implemented
- **WHEN** a user imports `std.camera.volume` and applies `Camera` and `FirstPersonCamera` to an entity
- **THEN** the compiler accepts the unit and filter clauses referencing these traits, even if the active rules are stubs
