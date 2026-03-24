## Requirements

### Requirement: std.physics.flat provides 2D kinematic physics traits
The `std.physics.flat` module SHALL provide traits and events for 2D kinematic (non-rigidbody) physics. `CharacterBody` holds velocity and ground state. `Collider` defines the entity's collision shape. The backend resolves collisions and emits `CollisionEnter` events when detected.

#### Scenario: CharacterBody fields for 2D
- **WHEN** `use std.physics.flat as phys` is imported and an entity has `phys.CharacterBody`
- **THEN** the entity has fields: `velocity: vec2`, `grounded: bool`, `gravity: float` with defaults `(0,0)`, `false`, `30.0`

#### Scenario: Backend applies gravity when not grounded
- **WHEN** a 2D entity has `CharacterBody` with `grounded = false`
- **THEN** the backend applies `gravity` per second to `velocity.y` on each `fixed_tick`

#### Scenario: Collider defines AABB bounds
- **WHEN** an entity has `phys.Collider` with `width = 32.0, height = 48.0`
- **THEN** the backend uses a 32×48 axis-aligned bounding box for collision detection

#### Scenario: CollisionEnter event fires on overlap
- **WHEN** two entities with `Collider` traits overlap
- **THEN** the backend emits `CollisionEnter` to both entities with `other: entity_id` and `overlap: vec2`

---

### Requirement: std.physics.volume provides 3D kinematic physics traits
The `std.physics.volume` module SHALL provide traits and events for 3D kinematic physics. The surface mirrors the flat module but uses `vec3` for velocity and ground normal. The `Collider` shape type is stubbed pending a `collider_shape` built-in type addition.

#### Scenario: CharacterBody fields for 3D
- **WHEN** `use std.physics.volume as phys` is imported and an entity has `phys.CharacterBody`
- **THEN** the entity has fields: `velocity: vec3`, `grounded: bool`, `ground_normal: vec3`, `step_height: float`

#### Scenario: Step height enables climbing small obstacles
- **WHEN** a 3D entity with `CharacterBody` hits a surface where step height ≤ `step_height`
- **THEN** the backend moves the entity up over the step rather than blocking movement

#### Scenario: CollisionEnter 3D event includes contact point and normal
- **WHEN** two 3D entities with `Collider` traits collide
- **THEN** the backend emits `CollisionEnter` with `other: entity_id`, `point: vec3`, `normal: vec3`

---

### Requirement: Physics traits are passive — no user systems required for simulation
The physics backend SHALL run the physics simulation step automatically during `fixed_tick` without requiring user-written systems. User systems MAY read and write `CharacterBody.velocity` to apply forces and movement intent.

#### Scenario: Setting velocity drives movement
- **WHEN** a user system writes `CharacterBody.velocity = vec3(5.0, 0.0, 0.0)` in `on fixed_tick`
- **THEN** the physics backend moves the entity by that velocity × dt, resolving collisions
