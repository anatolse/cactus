## Requirements

### Requirement: std.camera.flat provides 2D camera traits
The `std.camera.flat` module SHALL provide a `Camera` trait for 2D orthographic rendering. Exactly one entity with `Camera.active = true` is used as the active view each frame. A `FollowCamera` trait is provided for camera-entity tracking; the associated `FollowCameraSystem` updates the `Camera` offset on `late_tick`.

#### Scenario: Camera trait fields for 2D
- **WHEN** `use std.camera.flat as cam` is imported and an entity has `cam.Camera`
- **THEN** the entity has fields: `zoom: float`, `offset: vec2`, `rotation: float`, `active: bool` with defaults `1.0`, `(0,0)`, `0.0`, `true`

#### Scenario: Only one active camera is used
- **WHEN** multiple entities have `cam.Camera` but only one has `active = true`
- **THEN** the backend renders the world from the viewpoint of the entity with `active = true`

#### Scenario: FollowCamera drives camera offset
- **WHEN** an entity has both `cam.Camera` and `cam.FollowCamera` with a valid `target: entity_id`
- **THEN** `FollowCameraSystem` updates `Camera.offset` each `late_tick` to track the target's position with smoothing

---

### Requirement: std.camera.volume provides 3D camera traits and helper systems
The `std.camera.volume` module SHALL provide a `Camera` trait for 3D perspective rendering. Additional traits define common camera behaviors: `FollowCamera` (third-person follow), `FirstPersonCamera` (FPS look), and `ThirdPersonCamera` (orbit camera). Active systems on `late_tick` update the entity's volume Transform based on these traits.

#### Scenario: Camera trait fields for 3D
- **WHEN** `use std.camera.volume as cam` is imported and an entity has `cam.Camera`
- **THEN** the entity has fields: `fov_y: float`, `near: float`, `far: float`, `active: bool`

#### Scenario: FirstPersonCamera responds to look input
- **WHEN** a `FirstPersonCamera` entity's `pitch` and `yaw` fields are updated by a user input system
- **THEN** `FirstPersonCameraSystem` updates the entity's volume Transform rotation on `late_tick`

#### Scenario: ThirdPersonCamera orbits around target
- **WHEN** a `ThirdPersonCamera` entity has a valid `target: entity_id` with a volume Transform
- **THEN** `ThirdPersonCameraSystem` positions the camera at `target.position + offset(distance, height)` on `late_tick`

---

### Requirement: Camera systems are stubbed pending entity query mechanism
The active camera systems (`FollowCameraSystem`, `FirstPersonCameraSystem`, `ThirdPersonCameraSystem`) SHALL be declared in the module files but their implementation bodies SHALL be stubbed with a `# TODO: requires entity query` comment. The trait definitions SHALL be complete and usable. Users may implement camera logic manually in the interim using the camera traits directly.

#### Scenario: Camera traits usable before systems are implemented
- **WHEN** a user imports `std.camera.volume` and applies `Camera` and `FirstPersonCamera` to an entity
- **THEN** the compiler accepts the unit and filter clauses referencing these traits, even if the active systems are stubs

---

### Requirement: std.camera.flat.Camera entity drives the runtime active camera
When a Cactus module imports `std.camera.flat` and declares an entity with the `Camera` trait, the cpp-entt backend SHALL read that entity at the start of each frame (in `generated_update_project`) and translate it into a raylib `Camera2D` that is used for all world-space rendering in that frame. This makes the `Camera` trait functionally active rather than a data-only declaration.

The translation rules are:
- `Camera2D.target` ← `Camera.offset` (the world position that maps to screen center)
- `Camera2D.zoom` ← `Camera.zoom`
- `Camera2D.rotation` ← `Camera.rotation` (radians converted to degrees for raylib)
- `Camera2D.offset` ← `{GetScreenWidth()/2.0f, GetScreenHeight()/2.0f}` (world origin at screen center)

Only the first entity with `Camera.active == true` is used.

#### Scenario: Camera with zoom renders world-unit entities at correct screen size
- **WHEN** a Camera entity has `zoom=64.0` and an entity has `WorldTransform.position = {0,0}` with `Shape.size = {1,1}`
- **THEN** the entity is rendered as a 64×64 pixel rectangle at the screen center

#### Scenario: Camera with non-zero offset pans the view
- **WHEN** a Camera entity has `zoom=64.0` and `offset=vec2(3.0, 0.0)`
- **THEN** the world position `{3,0}` maps to screen center, and `{0,0}` maps 192px left of center

#### Scenario: Module without std.camera.flat uses identity camera
- **WHEN** a module does not import `std.camera.flat`
- **THEN** no camera entity is read and the runtime uses an identity Camera2D (zoom=1, no offset/target)
- **THEN** rendering behavior is identical to before this change (pixel-space coordinates)
