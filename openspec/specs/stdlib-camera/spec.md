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

### Requirement: Every 3D camera viewport keeps a CameraView
`std.camera.volume` SHALL declare a trait `CameraView` and a keep rule that keeps it on every entity with `Camera`, `std.camera.viewport.Viewport` and a volume `WorldTransform`. Its fields SHALL be:
- `position: vec3`, the camera's world position;
- `forward`, `right`, `up: vec3`, unit vectors with the same orientation the renderer uses for that camera;
- `fov_y`, `near`, `far: float`, copied from `Camera`;
- `rect_min`, `rect_size: vec2`, the viewport's rectangle in pixels, from `Viewport`'s normalized rectangle and `std.camera.viewport.Screen`'s size.

The keep rule SHALL belong to a public group `views` in `core.late_tick`, ordered after the stdlib camera rules and transform propagation. So in any phase, a view describes the camera as the last rendered frame showed it. An entity that loses one of the three traits SHALL lose `CameraView`. Game code SHALL NOT write `CameraView` (it is kept).

#### Scenario: View follows the camera
- **WHEN** a camera's `WorldTransform` moves between two late ticks
- **THEN** its `CameraView.position` and directions match the new pose after the second late tick

#### Scenario: Pixel rect from a split viewport
- **WHEN** the screen is 1280×720 and a camera's `Viewport` has `x = 0.5`, `width = 0.5`, `y = 0.0`, `height = 1.0`
- **THEN** its view has `rect_min = (640, 0)` and `rect_size = (640, 720)`

#### Scenario: Camera without a viewport
- **WHEN** an entity has `Camera` and `WorldTransform` but no `Viewport`
- **THEN** it has no `CameraView`

### Requirement: Projection functions are pure
`std.camera.volume` SHALL provide pure Cactus functions that take the `CameraView` trait itself as a parameter (`camera: CameraView`). They read only the view's fields:
- `to_screen(camera: CameraView, point: vec3) Projected`, where `Projected` has `position: vec2` in pixels and `in_front: bool`. `in_front` SHALL be false when the point is not farther than `near` along `forward`, and then `position` SHALL be `rect_min`.
- `ray(camera: CameraView, screen: vec2) ScreenRay`, where `ScreenRay` has `origin: vec3` on the near plane and a unit `direction: vec3` through the pixel `screen`. Pixels outside the rectangle SHALL extend the same projection.
- `ray_plane(ray, plane_origin, plane_normal) collision.RayHit`, with `hit` false when the ray is parallel to the plane or the plane is behind the ray.

For a view whose `rect_size` has a zero component, `to_screen` SHALL return `in_front = false`, and `ray` SHALL return `origin = position` and `direction = forward`. For every pixel `s` inside the rectangle and every positive distance `d` within `near` and `far`, `to_screen(camera, r.origin + r.direction * d).position` SHALL equal `s` within 0.01 pixels, where `r = ray(camera, s)`. Projection SHALL agree with the renderer's own perspective projection within 0.5 pixels.

#### Scenario: Round trip
- **WHEN** a game computes `r = ray(cv, vec2(300.0, 200.0))` and projects `r.origin + r.direction * 10.0` with `to_screen`
- **THEN** the result is `(300, 200)` within 0.01 pixels and `in_front` is true

#### Scenario: Point behind the camera
- **WHEN** a point lies behind the camera
- **THEN** `to_screen` returns `in_front = false`

#### Scenario: Matches the renderer
- **WHEN** a headless test renders a marker at a world point with the fake renderer's projection and calls `to_screen` for the same point
- **THEN** the two pixel positions differ by at most 0.5 pixels

#### Scenario: Ground under the cursor
- **WHEN** a rule binds a camera with `CameraView as cv` and computes `ray_plane(ray(cv, mouse), vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0))`
- **THEN** the hit point is the ground point under the mouse, and `hit` is false when the camera looks away from the ground

### Requirement: Screen-space queries are pair rules over cameras
Game code SHALL read a camera's projection only through a rule binding of the camera's `CameraView`: a unary rule over cameras, or a pair rule between cameras and other entities. `std.camera.volume` SHALL NOT provide functions that read an implicit active camera. `screen_to_plane` and `screen_delta_on_plane` SHALL NOT exist.

#### Scenario: Box selection
- **WHEN** a pair rule binds `camera` with `CameraView as cv` and `unit` with `Selectable` and `WorldTransform as t`, and its `where:` tests that `to_screen(cv, t.position)` is in front and inside a drag rectangle
- **THEN** `keep InBox on unit` marks exactly the units drawn inside that rectangle in that camera's viewport

#### Scenario: Removed helper
- **WHEN** a module calls `camera.screen_to_plane(...)`
- **THEN** compilation fails with an error that names `screen_to_plane` as unknown
