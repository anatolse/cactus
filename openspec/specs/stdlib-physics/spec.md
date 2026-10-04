## Purpose

Define the `std.physics` stdlib module, including 2D (`flat`) and 3D (`volume`) kinematic physics traits, collider-backed cast/overlap world queries with stable contact semantics, and the trait-filtered query namespace — all driven passively without user-authored rules.

## Requirements

### Requirement: std.physics.flat provides 2D kinematic physics traits
The `std.physics.flat` module SHALL provide traits and events for 2D kinematic (non-rigidbody) physics. `CharacterBody` holds velocity and ground state. `Collider` defines shared collision filtering data. Shape-specific collider traits define common 2D primitive bounds: `BoxCollider`, `CircleCollider`, and `CapsuleCollider`. The cpp-entt backend resolves supported 2D collider overlaps and emits `CollisionEnter` events when detected.

#### Scenario: CharacterBody fields for 2D
- **WHEN** `use std.physics.flat as phys` is imported and an entity has `phys.CharacterBody`
- **THEN** the entity has fields: `velocity: vec2`, `grounded: bool`, `gravity: float` with defaults `(0,0)`, `false`, `30.0`

#### Scenario: Backend applies gravity when not grounded
- **WHEN** a 2D entity has `CharacterBody` with `grounded = false` and the program is generated for the cpp-entt backend
- **THEN** the backend applies `gravity` per second to `velocity.y` on each `fixed_tick`

#### Scenario: Collider defines shared filtering data
- **WHEN** `use std.physics.flat as phys` is imported and an entity has `phys.Collider`
- **THEN** the entity has fields: `layer: int` and `mask: int` with defaults `1` and `1`

#### Scenario: BoxCollider defines rectangle bounds
- **WHEN** an entity has `phys.BoxCollider` with `size = vec2(32.0, 48.0)`
- **THEN** the cpp-entt backend uses a 32×48 axis-aligned box for collision detection at the entity's `std.transform.flat.WorldTransform.position`

#### Scenario: Square uses BoxCollider with equal dimensions
- **WHEN** an entity needs a square collider in 2D
- **THEN** it uses `phys.BoxCollider` with equal `size.x` and `size.y` values

#### Scenario: CircleCollider defines circular bounds
- **WHEN** an entity has `phys.CircleCollider` with `radius = 16.0`
- **THEN** the cpp-entt backend uses a circle with radius 16.0 for supported 2D collision detection

#### Scenario: CapsuleCollider defines 2D capsule bounds
- **WHEN** an entity has `phys.CapsuleCollider` with `radius = 8.0` and `height = 32.0`
- **THEN** the cpp-entt backend uses a vertical 2D capsule with the authored radius and height for supported collision detection

#### Scenario: Collider includes layer and mask filtering
- **WHEN** two 2D entities have `phys.Collider` traits
- **THEN** the cpp-entt backend treats them as collision candidates only when their `layer` and `mask` bitmasks allow the interaction

#### Scenario: CollisionEnter event fires on overlap
- **WHEN** two entities with compatible `Collider` traits overlap in a cpp-entt program
- **THEN** the backend emits `CollisionEnter` to both entities with `other: entity_id` and `overlap: vec2`

#### Scenario: Other backends are not required to simulate stdlib colliders
- **WHEN** a program imports `std.physics.flat` and applies `Collider` while targeting a backend other than cpp-entt
- **THEN** this change does not require that backend to perform runtime collision simulation

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
The `std.physics.volume` module SHALL provide traits and events for 3D kinematic physics. The surface mirrors the flat module where practical but uses `vec3` for velocity, normals, and 3D shape dimensions. `Collider` defines shared collision filtering data. Shape-specific collider traits define common 3D primitive bounds: `BoxCollider`, `SphereCollider`, and `CapsuleCollider`.

#### Scenario: CharacterBody fields for 3D
- **WHEN** `use std.physics.volume as phys` is imported and an entity has `phys.CharacterBody`
- **THEN** the entity has fields: `velocity: vec3`, `grounded: bool`, `ground_normal: vec3`, `step_height: float`

#### Scenario: Step height enables climbing small obstacles
- **WHEN** a 3D entity with `CharacterBody` hits a surface where step height ≤ `step_height`
- **THEN** the backend moves the entity up over the step rather than blocking movement

#### Scenario: Collider defines shared 3D filtering data
- **WHEN** `use std.physics.volume as phys` is imported and an entity has `phys.Collider`
- **THEN** the entity has fields: `layer: int` and `mask: int` with defaults `1` and `1`

#### Scenario: BoxCollider defines 3D box bounds
- **WHEN** an entity has `phys.BoxCollider` with `size = vec3(1.0, 2.0, 3.0)`
- **THEN** the cpp-entt backend uses a 1×2×3 axis-aligned box for supported 3D collision detection at the entity's `std.transform.volume.WorldTransform.position`

#### Scenario: Cube uses BoxCollider with equal dimensions
- **WHEN** an entity needs a cube collider in 3D
- **THEN** it uses `phys.BoxCollider` with equal `size.x`, `size.y`, and `size.z` values

#### Scenario: SphereCollider defines spherical bounds
- **WHEN** an entity has `phys.SphereCollider` with `radius = 1.5`
- **THEN** the cpp-entt backend uses a sphere with radius 1.5 for supported 3D collision detection

#### Scenario: CapsuleCollider defines 3D capsule bounds
- **WHEN** an entity has `phys.CapsuleCollider` with `radius = 0.5` and `height = 2.0`
- **THEN** the cpp-entt backend uses a vertical 3D capsule with the authored radius and height for supported collision detection

#### Scenario: CollisionEnter 3D event includes contact point and normal
- **WHEN** two 3D entities with compatible `Collider` and shape collider traits collide in a cpp-entt program
- **THEN** the backend emits `CollisionEnter` with `other: entity_id`, `point: vec3`, `normal: vec3`

---

### Requirement: Physics traits are passive — no user rules required for simulation
The physics backend SHALL run the physics simulation step automatically during `fixed_tick` without requiring user-written rules. User rules MAY read and write `CharacterBody.velocity` to apply forces and movement intent.

#### Scenario: Setting velocity drives movement
- **WHEN** a user rule writes `CharacterBody.velocity = vec3(5.0, 0.0, 0.0)` in `on fixed_tick`
- **THEN** the physics backend moves the entity by that velocity × dt, resolving collisions

### Requirement: std.physics.flat exposes trait-filtered query namespace
The `std.physics.flat` stdlib surface SHALL expose a `query` namespace containing 2D spatial query expressions. These queries SHALL support bracketed trait filters and named geometry arguments.

#### Scenario: Flat query namespace is available
- **WHEN** authored code imports `std.physics.flat.query`
- **THEN** 2D spatial queries such as `nearest` and `overlap_box` are available in expression position

#### Scenario: Flat nearest query supports trait filters
- **WHEN** authored code uses `query.nearest[Transform, Enemy](from = p)`
- **THEN** the query result is filtered by both 2D spatial proximity and the listed traits

#### Scenario: Flat circle overlap query is available
- **WHEN** authored code uses `query.overlap_circle[Pickup](center = p, radius = 24.0)`
- **THEN** the query performs a 2D circle-overlap search filtered by the listed traits

#### Scenario: Flat raycast query is available
- **WHEN** authored code uses `query.raycast[Wall](origin = p, dir = d, max_dist = 100.0)`
- **THEN** the query performs a 2D raycast filtered by the listed traits

### Requirement: std.physics.volume exposes trait-filtered query namespace
The `std.physics.volume` stdlib surface SHALL expose a `query` namespace containing 3D spatial query expressions. These queries SHALL mirror the flat namespace shape while using 3D spatial values.

#### Scenario: Volume query namespace is available
- **WHEN** authored code imports `std.physics.volume.query`
- **THEN** 3D spatial queries are available in expression position

#### Scenario: Volume nearest query uses 3D input
- **WHEN** authored code uses `query.nearest[Transform, Enemy](from = p3)` from the volume namespace
- **THEN** the query interprets `from` as a 3D spatial point and matches only entities satisfying the listed trait filters

#### Scenario: Volume sphere overlap query is available
- **WHEN** authored code uses `query.overlap_sphere[Pickup](center = p3, radius = 2.0)`
- **THEN** the query performs a 3D sphere-overlap search filtered by the listed traits

#### Scenario: Volume raycast query is available
- **WHEN** authored code uses `query.raycast[Wall](origin = p3, dir = d3, max_dist = 100.0)`
- **THEN** the query performs a 3D raycast filtered by the listed traits

### Requirement: std.physics.volume provides a shape-agnostic sweep between rule bindings
The `std.physics.volume` module SHALL provide `SweepHit` with fields `hit: bool`, `other: entity_id`, `t: float`, `point: vec3`, and `normal: vec3`, and a pure function `sweep(subject, delta: vec3, target) SweepHit`. `sweep` SHALL report where the subject's collider first touches the target's collider when the subject moves by `delta` from its current position, whatever shape kinds the two colliders have. `subject` and `target` SHALL each be a binding of the enclosing rule; any other `entity_id` expression in those positions SHALL be a compile error.

#### Scenario: Sweep hits any shape kind
- **WHEN** a sphere-collider subject is swept toward a box target, a sphere target, and a capsule target in turn
- **THEN** each sweep reports `hit = true` with `other` set to that target

#### Scenario: Sweep miss
- **WHEN** the target lies outside the path of the subject's collider over `delta`
- **THEN** the result has `hit = false`

#### Scenario: Sweep does not pass through thin geometry
- **WHEN** a subject is swept along a `delta` longer than its own size across a box thinner than that `delta`
- **THEN** the result has `hit = true` on that box

#### Scenario: Sweep starting in contact
- **WHEN** the subject already overlaps the target before moving
- **THEN** the result has `hit = true`, `t = 0.0`, and `normal` pointing in the direction that pushes the subject out

#### Scenario: Zero delta tests overlap only
- **WHEN** `delta` is `vec3(0.0, 0.0, 0.0)`
- **THEN** the result is a hit at `t = 0.0` if the colliders overlap, and a miss otherwise

#### Scenario: Non-binding argument rejected
- **WHEN** a rule calls `physics.sweep(self, step, Game)` where `Game` is a named entity, not a binding of the rule
- **THEN** compilation reports a source-located error

### Requirement: std.physics.volume provides a shape-agnostic overlap predicate between rule bindings
The `std.physics.volume` module SHALL provide a pure function `touching(a, b) bool` that is true when the two bindings' colliders overlap at their current positions, whatever shape kinds they have. `a` and `b` SHALL each be a binding of the enclosing rule.

#### Scenario: Touching across shape kinds
- **WHEN** a capsule collider and a rotated box collider overlap
- **THEN** `touching` returns true

#### Scenario: Not touching
- **WHEN** a sphere lies in the corner region of a box's axis-aligned bounds but outside the box itself
- **THEN** `touching` returns false

### Requirement: Sweep hits define stable contact semantics
`sweep` SHALL fill `SweepHit` with stable semantics suitable for movement and hit logic.

#### Scenario: Hit fields
- **WHEN** a sweep reports a hit
- **THEN** `t` is the fraction of `delta` travelled before contact, in `[0.0, 1.0]`
- **AND** `point` is the contact location on the target's surface
- **AND** `normal` is unit length and points from the target toward the subject

#### Scenario: Miss fields
- **WHEN** a sweep reports a miss
- **THEN** `hit` is false, `other` is a stale `entity_id`, `t` is `1.0`, and `point` and `normal` are zero vectors
- **AND** moving the subject by `delta * t` moves it the full `delta`

#### Scenario: Centered box geometry
- **WHEN** a box target with `size = vec3(2.0, 2.0, 2.0)` sits at the origin and a sphere subject of radius `0.5` at `vec3(-5.0, 0.0, 0.0)` is swept by `vec3(10.0, 0.0, 0.0)`
- **THEN** the hit has `t = 0.35`, `point = vec3(-1.0, 0.0, 0.0)`, and `normal = vec3(-1.0, 0.0, 0.0)`

### Requirement: Collider queries honor filtering and identity
`sweep` and `touching` SHALL treat a pair as non-colliding when the subject's `Collider.mask` shares no bit with the target's `Collider.layer`, when subject and target are the same entity, or when either entity lacks `std.transform.volume.WorldTransform`, `Collider`, or a shape collider trait at evaluation time. For `touching`, the first argument is the subject.

#### Scenario: Mask excludes a layer
- **WHEN** the subject's mask is `3` and the target's layer is `4`
- **THEN** `sweep` reports a miss and `touching` returns false, whatever the geometry

#### Scenario: An entity never hits itself
- **WHEN** a rule's two bindings refer to the same entity
- **THEN** `sweep` reports a miss and `touching` returns false

#### Scenario: Missing collider data is a miss
- **WHEN** the target has lost its shape collider trait
- **THEN** `sweep` reports a miss without error

### Requirement: 3D collider queries use the authored shapes
`sweep` and `touching` SHALL test the authored shapes, not bounding boxes. A box SHALL be centered on `WorldTransform.position` and oriented by `WorldTransform.rotation`, with `BoxCollider.size` as its full world size. A sphere SHALL be centered on `WorldTransform.position`. A capsule SHALL be vertical along world Y, centered on `WorldTransform.position`, with `CapsuleCollider.height` as its total height including the caps. `WorldTransform.scale` SHALL NOT change any shape.

#### Scenario: Rotated box uses its oriented shape
- **WHEN** a box with `size = vec3(4.0, 1.0, 0.2)` is rotated 45 degrees around Y
- **AND** a small sphere sits inside the box's axis-aligned bounds but outside the rotated box
- **THEN** `touching` between them returns false

#### Scenario: Capsule is not its bounding box
- **WHEN** a sphere sits next to a capsule's cap, inside the capsule's bounding box but outside the capsule
- **THEN** `touching` returns false

### Requirement: Colliders declare exactly one shape
A template or entity declaration that applies `std.physics.volume.Collider` SHALL also apply exactly one of `BoxCollider`, `SphereCollider`, or `CapsuleCollider`, counting traits gained through `use` and `from`. Declaring none or more than one SHALL be a compile error.

#### Scenario: Collider without a shape
- **WHEN** a template applies `physics.Collider` and no shape collider trait
- **THEN** compilation reports a source-located error naming the template

#### Scenario: Two shapes on one collider
- **WHEN** a template applies `physics.Collider`, `physics.BoxCollider`, and `physics.SphereCollider`
- **THEN** compilation reports a source-located error naming both shape traits
