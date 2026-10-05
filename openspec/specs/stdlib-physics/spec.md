## Purpose

Define the `std.physics` stdlib module, including 2D (`flat`) and 3D (`volume`) kinematic physics traits, collider-backed cast/overlap world queries with stable contact semantics, and the trait-filtered query namespace — all driven passively without user-authored rules.
## Requirements
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

### Requirement: Physics traits are passive — no user rules required for simulation
For 3D, the `std.physics.volume` stdlib controller (`stdlib-character-body`) SHALL move every `CharacterBody` during `fixed_tick` without requiring user-written rules. User rules MAY write `CharacterBody.velocity` to apply movement intent and impulses. 2D `CharacterBody` is not simulated.

#### Scenario: Setting velocity drives movement
- **WHEN** a user rule writes `CharacterBody.velocity = vec3(5.0, 0.0, 0.0)` in `on fixed_tick` for a 3D body
- **THEN** the stdlib controller moves the entity by that velocity × dt in the same tick, resolving collisions

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
The `std.physics.volume` module SHALL provide `SweepHit` with fields `hit: bool`, `other: entity_id`, `t: float`, `point: vec3`, `normal: vec3`, and `surface: vec3`, and a pure function `sweep(subject, delta: vec3, target) SweepHit`. `sweep` SHALL report where the subject's collider first touches the target's collider when the subject moves by `delta` from its current position, whatever shape kinds the two colliders have. `normal` SHALL point from the target toward the subject at the contact. `surface` SHALL be the target's surface normal at the contact: for a box contact on an edge or corner, the normal of the adjacent face that `delta` meets most directly (with zero `delta`, the face closest to `normal`); for a sphere or capsule target, equal to `normal`. A miss SHALL have zero `normal` and `surface`. A subject that already touches or overlaps the target SHALL be blocked at `t = 0.0` only when a non-zero `delta` moves it further into the target; motion out of or along the contact SHALL NOT be blocked by that contact. `subject` and `target` SHALL each be a binding of the enclosing rule; any other `entity_id` expression in those positions SHALL be a compile error.

#### Scenario: Sweep hits any shape kind
- **WHEN** a sphere-collider subject is swept toward a box target, a sphere target, and a capsule target in turn
- **THEN** each sweep reports `hit = true` with `other` set to that target

#### Scenario: Landing on a box edge reports the top face
- **WHEN** a capsule whose axis is just outside a box's top edge is swept straight down onto that edge
- **THEN** `normal` is tilted away from the box and `surface` is the box's top-face normal

#### Scenario: Running into a box edge reports the side face
- **WHEN** a capsule is swept horizontally into a box's top edge
- **THEN** `surface` is the normal of the box face it moves toward

#### Scenario: Sweep miss
- **WHEN** the target lies outside the path of the subject's collider over `delta`
- **THEN** the result has `hit = false`

#### Scenario: Sweep does not pass through thin geometry
- **WHEN** a subject is swept along a `delta` longer than its own size across a box thinner than that `delta`
- **THEN** the result has `hit = true` on that box

#### Scenario: Sweep starting in contact
- **WHEN** the subject already overlaps the target and `delta` points further into it
- **THEN** the result has `hit = true`, `t = 0.0`, and `normal` pointing in the direction that pushes the subject out

#### Scenario: Sweep starting in contact moving outward
- **WHEN** the subject already overlaps the target and `delta` points along the push-out normal
- **THEN** the result has `hit = false`

#### Scenario: Sliding along a resting contact
- **WHEN** a sphere rests on top of a box with a gap of at most the contact skin and `delta` is horizontal
- **THEN** the result has `hit = false`

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
`sweep`, `touching`, and `push_out` SHALL treat a pair as non-colliding when the subject's `Collider.mask` shares no bit with the target's `Collider.layer`, when subject and target are the same entity, or when either entity lacks `std.transform.volume.WorldTransform`, `Collider`, or a shape collider trait at evaluation time. For `touching` and `push_out`, the first argument is the subject.

#### Scenario: Mask excludes a layer
- **WHEN** the subject's mask is `3` and the target's layer is `4`
- **THEN** `sweep` reports a miss, `touching` returns false, and `push_out` returns the zero vector, whatever the geometry

#### Scenario: An entity never hits itself
- **WHEN** a rule's two bindings refer to the same entity
- **THEN** `sweep` reports a miss, `touching` returns false, and `push_out` returns the zero vector

#### Scenario: Missing collider data is a miss
- **WHEN** the target has lost its shape collider trait
- **THEN** `sweep` reports a miss and `push_out` returns the zero vector, without error

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

### Requirement: std.physics.volume provides a shape-agnostic push-out between rule bindings
The `std.physics.volume` module SHALL provide a pure function `push_out(a, b) vec3` that returns the shortest translation that moves `a`'s collider out of overlap with `b`'s collider, whatever shape kinds they have, and the zero vector when they don't overlap. When both entities have `CharacterBody` and each one's `Collider.mask` shares a bit with the other's `Collider.layer`, the result SHALL be half of that translation. `a` and `b` SHALL each be a binding of the enclosing rule; any other `entity_id` expression in those positions SHALL be a compile error.

#### Scenario: Sphere overlapping a box
- **WHEN** a sphere of radius `0.5` at `vec3(-1.3, 0.0, 0.0)` overlaps a box of size `vec3(2.0, 2.0, 2.0)` at the origin
- **THEN** `push_out(sphere, box)` is `vec3(-0.2, 0.0, 0.0)` within float tolerance

#### Scenario: No overlap
- **WHEN** the two colliders are separated
- **THEN** `push_out` returns `vec3(0.0, 0.0, 0.0)`

#### Scenario: Two mutual character bodies share the push
- **WHEN** two capsule `CharacterBody` entities on layer `1` with mask `1` overlap by `0.2` along X
- **THEN** `push_out(a, b)` has length `0.1` and points from `b` toward `a`

#### Scenario: One-sided character body takes the full push
- **WHEN** body `a`'s mask selects `b`'s layer but `b`'s mask does not select `a`'s layer
- **THEN** `push_out(a, b)` is the full translation

#### Scenario: Non-binding argument rejected
- **WHEN** a rule calls `physics.push_out(self, Game)` where `Game` is a named entity, not a binding of the rule
- **THEN** compilation reports a source-located error

### Requirement: std.physics.flat provides 2D kinematic physics and trigger traits
The `std.physics.flat` module SHALL provide traits and events for 2D kinematic (non-rigidbody) physics. `CharacterBody` holds velocity and ground state as plain data; no stdlib or backend code moves a 2D `CharacterBody`, and the module SHALL NOT claim that it does. `Collider` defines shared collision filtering data, and `Solid` marks a collider as solid; a collider without `Solid` is a trigger. Shape-specific collider traits define common 2D primitive bounds: `BoxCollider`, `CircleCollider`, and `CapsuleCollider`. The module SHALL NOT declare collision events.

#### Scenario: CharacterBody fields for 2D
- **WHEN** `use std.physics.flat as phys` is imported and an entity has `phys.CharacterBody`
- **THEN** the entity has fields: `velocity: vec2`, `grounded: bool`, `gravity: float` with defaults `(0,0)`, `false`, `30.0`

#### Scenario: Backend applies gravity when not grounded
- **WHEN** a 2D entity has `CharacterBody` with `grounded = false`, a non-zero `velocity`, and no game rule moves it
- **THEN** no backend or stdlib code applies gravity: its position and `CharacterBody` fields do not change

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

#### Scenario: Solid marker for 2D
- **WHEN** `use std.physics.flat as phys` is imported
- **THEN** the module declares a fieldless trait `phys.Solid`, and declares no `CollisionEnter` event

#### Scenario: Other backends are not required to simulate stdlib colliders
- **WHEN** a program imports `std.physics.flat` and applies `Collider` while targeting a backend other than cpp-entt
- **THEN** this change does not require that backend to perform runtime collision simulation

### Requirement: std.physics.volume provides 3D kinematic physics and trigger traits
The `std.physics.volume` module SHALL provide traits and events for 3D kinematic physics. The surface mirrors the flat module where practical but uses `vec3` for velocity, normals, and 3D shape dimensions. `CharacterBody` holds the intent a game writes, the results the stdlib controller writes (`stdlib-character-body`), and scratch fields that only the controller writes. `Collider` defines shared collision filtering data, and `Solid` marks a collider as solid; a collider without `Solid` is a trigger (see "Colliders without Solid are triggers"). The module SHALL NOT declare collision events. Shape-specific collider traits define common 3D primitive bounds: `BoxCollider`, `SphereCollider`, and `CapsuleCollider`.

#### Scenario: CharacterBody fields for 3D
- **WHEN** `use std.physics.volume as phys` is imported and an entity has `phys.CharacterBody`
- **THEN** the entity has game-facing fields `velocity: vec3 = (0,0,0)`, `gravity: float = 9.81`, `step_height: float = 0.3`, `max_slope: float = 45.0`, `grounded: bool = false`, `ground_normal: vec3 = (0,1,0)`, and `time_since_grounded: float = 0.0`
- **AND** the controller-owned scratch fields `motion: vec3`, `lift: float`, `drop: float`, and `ground_offset: float`

#### Scenario: Step height enables climbing small obstacles
- **WHEN** a grounded 3D `CharacterBody` walks into a walkable step whose top is at most `step_height` above its feet
- **THEN** the stdlib controller moves the entity up onto the step rather than blocking movement

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

#### Scenario: Solid marker for 3D
- **WHEN** `use std.physics.volume as phys` is imported
- **THEN** the module declares a fieldless trait `phys.Solid`, and declares no `CollisionEnter` event

### Requirement: Colliders without Solid are triggers
An entity with `Collider` and a shape collider SHALL be solid when it also has `Solid`, and a trigger otherwise. The stdlib character controller SHALL treat only solids as obstacles. `touching`, `sweep` and `push_out` SHALL accept any collider, solid or trigger, and SHALL keep honoring `Collider.layer` and `mask`. Adding or removing `Solid` SHALL take effect at the next `fixed_tick` after its commit.

#### Scenario: Body walks into a trigger
- **WHEN** a character body walks into a box collider without `Solid`
- **THEN** the body moves into the box unblocked, and `physics.touching(body, box)` is true

#### Scenario: Body is blocked by a solid
- **WHEN** the same box has `Solid`
- **THEN** the controller stops the body at the box's surface

#### Scenario: Queries see triggers
- **WHEN** a bullet sweeps toward a trigger collider whose layer its mask selects
- **THEN** `sweep` reports a hit on the trigger

### Requirement: std.physics.volume publishes a contacts group
`std.physics.volume` SHALL declare a public group `contacts` bound to `fixed_tick` and ordered after `solve`, including the step that moves body descendants. A rule in `physics.contacts` SHALL see this tick's body poses and their descendants' poses.

#### Scenario: Contact rule sees this tick's pose
- **WHEN** a body moves into a trigger during `solve` and a keep rule in `group: physics.contacts` tests `physics.touching` against it
- **THEN** the kept trait is added at the same `fixed_tick`'s commit

