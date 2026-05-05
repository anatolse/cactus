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

### Requirement: std.physics.flat provides collider-backed 2D world query result types
The `std.physics.flat` module SHALL provide public query result types that represent collider-backed 2D query outcomes as an algebraic neutral-or-hit value, without using invalid `entity_id` values as miss sentinels.

#### Scenario: Query result types are available from std.physics.flat
- **WHEN** `use std.physics.flat as phys` is imported
- **THEN** `phys.QueryResultKind` is available with `Empty` and `Hit` variants
- **AND** `phys.QueryContact2D` is available with `entity: entity_id`, `normal: vec2`, `distance: float`, and `overlap: vec2`
- **AND** `phys.QueryResult2D` is available with `kind: QueryResultKind` and `contact: QueryContact2D`

#### Scenario: Empty result is the neutral query result
- **WHEN** a single-result world query finds no matching contact
- **THEN** it returns `QueryResult2D` with `kind = QueryResultKind.Empty`
- **AND** gameplay code does not need to test `exists(result.contact.entity)` to detect the miss

### Requirement: std.physics.flat provides entity-collider 2D cast queries
The `std.physics.flat` module SHALL provide a collider-backed `query_cast_nearest(subject: entity_id, delta: vec2, mask: int, exclude: entity_id) QueryResult2D` extern function. The query SHALL infer the swept shape from the `subject` entity's `WorldTransform`, `Collider`, and supported 2D shape collider trait.

#### Scenario: Cast query returns nearest hit
- **WHEN** `query_cast_nearest` is called with a subject entity, non-zero delta, collision mask, and excluded entity
- **AND** multiple matching candidate colliders would be contacted along the sweep
- **THEN** the returned `QueryResult2D` has `kind = QueryResultKind.Hit`
- **AND** its `contact` describes the candidate with the smallest travel distance along `delta`

#### Scenario: Cast query miss returns Empty
- **WHEN** `query_cast_nearest` is called and no matching collider is contacted along `delta`
- **THEN** the returned `QueryResult2D` has `kind = QueryResultKind.Empty`

#### Scenario: Cast query excludes requested entity
- **WHEN** `query_cast_nearest(subject, delta, mask, exclude)` is called
- **THEN** the collider belonging to `exclude` is not returned as the hit contact

### Requirement: std.physics.flat provides entity-collider 2D overlap queries
The `std.physics.flat` module SHALL provide collider-backed overlap query extern functions that infer the query shape from a subject entity's `WorldTransform`, `Collider`, and supported 2D shape collider trait.

#### Scenario: Deepest overlap query returns strongest contact
- **WHEN** `query_overlap_deepest(subject, mask, exclude)` is called and multiple matching candidate colliders overlap the subject
- **THEN** the returned `QueryResult2D` has `kind = QueryResultKind.Hit`
- **AND** its `contact` describes the candidate with the greatest overlap depth according to the backend's 2D contact calculation

#### Scenario: Deepest overlap query miss returns Empty
- **WHEN** `query_overlap_deepest(subject, mask, exclude)` is called and no matching collider overlaps the subject
- **THEN** the returned `QueryResult2D` has `kind = QueryResultKind.Empty`

#### Scenario: All overlap query returns contact list
- **WHEN** `query_overlap_all(subject, mask, exclude)` is called
- **THEN** it returns `list[QueryContact2D]` containing every matching overlap contact except the excluded entity
- **AND** when no matching collider overlaps the subject, it returns an empty list

### Requirement: 2D query contacts define stable contact semantics
Collider-backed 2D world queries SHALL populate `QueryContact2D` fields with stable semantics suitable for gameplay movement and interaction logic.

#### Scenario: Contact normal points out of hit collider
- **WHEN** a query returns a hit contact
- **THEN** `contact.normal` points from the hit collider toward the subject/query shape, representing the direction that pushes the subject out of the hit collider

#### Scenario: Cast distance is scalar travel distance
- **WHEN** `query_cast_nearest` returns a hit contact
- **THEN** `contact.distance` is the scalar world-unit distance the subject can travel along `delta` before contact

#### Scenario: Overlap vector describes separation
- **WHEN** an overlap query returns a hit contact
- **THEN** `contact.overlap` describes the minimum translation/separation vector for the overlap contact using the same direction convention as `contact.normal`

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
