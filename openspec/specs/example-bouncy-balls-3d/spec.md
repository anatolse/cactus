# example-bouncy-balls-3d Specification

## Purpose

Define the bouncy-balls-3d Cactus example: a 3D evolution of bouncy-bubbles demonstrating cross-domain and same-domain pair rules for sphere-vs-oriented-box and sphere-vs-sphere collision response generalized to `vec3`/`quat`, a fully enclosed 6-face box with one collider-only face, and mesh-based 3D rendering with per-entity color and point-light illumination.

## Requirements

### Requirement: Bouncy balls 3D Cactus module

The example SHALL provide a single `examples/bouncy-balls-3d/main.cactus` file that is valid Cactus DSL. It SHALL declare a `Ball` trait with only a `velocity: vec3` field, a `SphereCollider` trait with a `radius: float` field, an empty `Wall` marker trait, and a `BoxCollider` trait with a `size: vec3` field. Collider traits SHALL be independent of `std.render.meshes.Renderer` and SHALL NOT duplicate their data onto `Ball` or `Wall`. The module SHALL declare 6 `Wall` entities forming a closed box boundary and 8 `Ball` entities with distinct `SphereCollider.radius` values and initial `velocity`.

#### Scenario: Module compiles without errors
- **WHEN** the compiler is invoked with `cactus examples/bouncy-balls-3d/main.cactus --backend cpp-entt -o bouncy-balls-3d.generated.h`
- **THEN** the command exits with code 0 and produces valid generated C++ output

#### Scenario: Eight balls with distinct radii
- **WHEN** the module's entity declarations are inspected
- **THEN** exactly 8 entities apply `Ball` and `SphereCollider`, and no two of them declare the same `SphereCollider.radius` value

#### Scenario: Six walls form a closed box boundary
- **WHEN** the module's entity declarations are inspected
- **THEN** exactly 6 entities apply `Wall` and `BoxCollider`, together enclosing the balls' spawn volume, and at least one of the 6 walls has a `WorldTransform.rotation` that is not a cardinal-axis-aligned orientation

#### Scenario: Collider data is not duplicated on gameplay traits
- **WHEN** the `Ball` and `Wall` trait declarations are inspected
- **THEN** `Ball` declares only `velocity` and `Wall` declares no fields — radius and size live exclusively on `SphereCollider` and `BoxCollider`

### Requirement: Wall and ball entities are template-backed

The example SHALL declare a `template` archetype for walls and a `template` archetype for balls, each bundling the traits and default field values shared by every instance of that kind. Every `Wall` entity and every `Ball` entity in the module SHALL be declared as a template-backed entity (`entity Name from <Template>:`) rather than an inline `entity Name:` declaration, overriding only the fields that differ per instance.

#### Scenario: Wall entities share one template
- **WHEN** the module's wall entity declarations are inspected
- **THEN** all 6 wall entities are declared `from` the same wall template, each overriding only its `position` and `rotation` (and, for the one collider-only wall, omitting the rendering trait it would otherwise inherit)

#### Scenario: Ball entities share one template
- **WHEN** the module's ball entity declarations are inspected
- **THEN** all 8 ball entities are declared `from` the same ball template, each overriding only the fields that vary per ball (at minimum `position`, `velocity`, `SphereCollider.radius`, and render color)

### Requirement: Cross-domain pair rule detects ball-wall contact

The example SHALL declare a rule with a `pairs:` domain binding a `Ball`/`SphereCollider`-selecting binding against a `Wall`/`BoxCollider`-selecting binding (two different entity sets), detecting sphere-vs-oriented-box overlap using the wall's current `WorldTransform.rotation` (a `quat`, not assuming world-axis alignment), and emitting a targeted bounce event to the ball binding only when the ball is moving toward the wall along the wall's current contact normal.

#### Scenario: Overlapping and approaching pair emits a targeted event
- **WHEN** a ball's sphere overlaps a wall's oriented box on `tick` and the ball's velocity has a positive closing component along the wall's current contact normal
- **THEN** the pair handler emits a targeted bounce event to the ball binding carrying the wall's current contact normal

#### Scenario: Separating pair does not re-emit
- **WHEN** a ball still overlaps a wall but its velocity's closing component along the wall's current contact normal is zero or negative
- **THEN** the pair handler does not emit a bounce event for that tuple

#### Scenario: Detection is correct regardless of wall rotation
- **WHEN** a wall's `WorldTransform.rotation` is not a cardinal-axis-aligned orientation
- **THEN** the pair handler still correctly detects sphere-vs-box overlap by evaluating the ball's position in the wall's rotated local space (via the wall's `quat`), not world-axis-aligned space

#### Scenario: Pair handler does not mutate durable ball or wall traits
- **WHEN** the wall-contact pair handler body is inspected
- **THEN** it contains no assignment or compound assignment to `Ball`, `Wall`, `SphereCollider`, or `BoxCollider` fields through either pair binding

#### Scenario: Collider-only wall still participates in collision
- **WHEN** the wall with no rendering trait is inspected as a pair-rule binding target
- **THEN** it is still selected by the `Wall`/`BoxCollider` binding and can emit bounce events to overlapping, approaching balls identically to the other 5 walls

### Requirement: Wall contact normal is derived from orientation

The `Wall` trait SHALL NOT store an authored normal field. Each wall's outward contact normal SHALL be computed by rotating a fixed local-space outward direction by the wall's current `WorldTransform.rotation` (a `quat`), so the effective normal always matches the wall's current orientation with no separately-authored value that can drift out of sync.

#### Scenario: Derived normal matches a cardinal-aligned wall
- **WHEN** a wall's `WorldTransform.rotation` is a cardinal-axis-aligned orientation
- **THEN** its derived contact normal points along the corresponding world axis, matching what an equivalent hand-authored axial normal would have been

#### Scenario: Derived normal follows a non-cardinal wall
- **WHEN** a wall's `WorldTransform.rotation` is not a cardinal-axis-aligned orientation
- **THEN** its derived contact normal is the corresponding rotation of the local outward direction, and is not aligned to any single world axis

### Requirement: Unary rule resolves wall bounces with reflected velocity

The example SHALL declare a unary rule, filtered on `Ball`, that consumes the wall-bounce event and reflects the entity's `velocity` across the event's `normal`, conserving speed (elastic reflection).

#### Scenario: Velocity reflects across the wall normal
- **WHEN** the wall-bounce resolver handles an event with a given `normal` for a ball with incoming `velocity`
- **THEN** the ball's `velocity` is updated to the mirror of the incoming velocity about `normal`
- **AND** the magnitude of the updated velocity equals the magnitude of the incoming velocity

### Requirement: Same-domain pair rule detects ball-ball contact

The example SHALL declare a rule with a `pairs:` domain binding two `Ball`/`SphereCollider`-selecting bindings drawn from the same entity set, computing a mass-weighted elastic collision response and emitting a targeted bounce event carrying the resolved post-collision velocity to each participating ball independently.

#### Scenario: Same-domain pair includes self-pairs and both directions
- **WHEN** the ball-ball pair rule's declared bindings are inspected
- **THEN** both bindings select the `Ball` and `SphereCollider` traits, matching the shipped `dsl-pair-relations` semantics that such a relation includes `(a,a)`, `(a,b)`, `(b,a)`, and `(b,b)` tuples for two live balls `a` and `b`

#### Scenario: Self-pairs and non-overlapping pairs are rejected
- **WHEN** a tuple's two bindings resolve to the same entity, or the two balls' spheres do not overlap
- **THEN** the pair handler does not emit a bounce event for that tuple

#### Scenario: Approaching, overlapping pair emits one targeted event per side
- **WHEN** two distinct balls overlap and are closing (positive relative velocity along the line between their centers)
- **THEN** the tuple with binding order `(a,b)` emits a targeted bounce event to `a` carrying `a`'s resolved post-collision velocity
- **AND** the reverse tuple `(b,a)` emits a targeted bounce event to `b` carrying `b`'s resolved post-collision velocity

#### Scenario: Heavier ball imparts more change on a lighter ball
- **WHEN** two balls of different `SphereCollider.radius` collide head-on
- **THEN** the resolved post-collision velocities are computed using mass proportional to `radius * radius` for each ball, so the smaller ball's velocity changes more than the larger ball's

### Requirement: Unary rule applies resolved ball-ball velocity

The example SHALL declare a unary rule, filtered on `Ball`, that consumes the ball-ball bounce event and assigns the event's resolved velocity directly to the entity's `velocity`.

#### Scenario: Resolved velocity is applied
- **WHEN** the ball-ball resolver handles a bounce event carrying a resolved velocity for a ball
- **THEN** that ball's `velocity` is set to the event's resolved velocity

### Requirement: Balls move by constant-velocity drift with no gravity

The example SHALL integrate each ball's position from its current `velocity` alone on every `tick` (`position += velocity * tick.dt`), with no acceleration or gravity term applied at any point, so balls neither settle nor speed up over time outside of collisions.

#### Scenario: Position integrates from velocity alone
- **WHEN** a ball's movement rule runs on `tick` with no bounce event pending that tick
- **THEN** its position changes by exactly `velocity * tick.dt`, and its `velocity` is unchanged by the movement rule itself

### Requirement: Balls render as spheres and walls render as boxes via std.render.meshes

The example SHALL apply `std.render.meshes.Renderer` to each `Ball` entity, using a `mesh` asset whose declared path resolves to placeholder sphere geometry, and to 5 of the 6 `Wall` entities, using a `mesh` asset that resolves to placeholder box geometry. Each `Ball` entity SHALL set a distinct `Renderer.color`. Each entity's `tv.WorldTransform.scale` SHALL correspond to its own collider dimensions: a `Ball` entity's scale SHALL equal `vec3(radius, radius, radius)` where `radius` is that same entity's `SphereCollider.radius`, and a `Wall` entity's scale SHALL equal that same entity's `BoxCollider.size` exactly. The sixth `Wall` entity (the one the camera looks through) SHALL declare no `std.render.meshes.Renderer` (or any other rendering trait) at all.

#### Scenario: Ball mesh asset resolves to sphere geometry
- **WHEN** a `Ball` entity's `Renderer.mesh` asset declaration is inspected
- **THEN** its declared path contains the substring `sphere`

#### Scenario: Ball entities have distinct colors
- **WHEN** the 8 `Ball` entities' `Renderer.color` values are inspected
- **THEN** no two balls declare the same `Renderer.color` value

#### Scenario: Rendered wall has no sphere-shaped mesh asset
- **WHEN** a rendered `Wall` entity's `Renderer.mesh` asset declaration is inspected
- **THEN** its declared path does not contain the substring `sphere`, so it resolves to placeholder box geometry

#### Scenario: One wall has no renderer
- **WHEN** the module's 6 wall entities are inspected for rendering traits
- **THEN** exactly 5 declare `std.render.meshes.Renderer` and exactly 1 declares no rendering trait

#### Scenario: Ball render scale matches its collider radius
- **WHEN** a `Ball` entity's `tv.WorldTransform.scale` is inspected
- **THEN** it equals `vec3(r, r, r)` where `r` is that same entity's `SphereCollider.radius`

#### Scenario: Wall render scale matches its collider size
- **WHEN** a `Wall` entity's `tv.WorldTransform.scale` is inspected
- **THEN** it equals that same entity's `BoxCollider.size` exactly

#### Scenario: Distinct ball radii produce distinct render scales
- **WHEN** the 8 `Ball` entities' `tv.WorldTransform.scale` values are inspected
- **THEN** no two balls declare the same scale value, matching their already-distinct `SphereCollider.radius` values

### Requirement: Scene lighting uses two point lights with no shadow casting

The example SHALL declare exactly two `std.render.meshes.PointLight` entities positioned to illuminate the box interior. The example SHALL NOT depend on shadow-casting behavior for correctness or visual completeness.

#### Scenario: Two point lights illuminate the scene
- **WHEN** the module's entity declarations are inspected
- **THEN** exactly 2 entities apply `std.render.meshes.PointLight`, both with `enabled = true`

### Requirement: Static camera views the box through the open wall

The example SHALL declare a single static `std.camera.volume.Camera` + `std.camera.viewport.Viewport` entity — no `FollowCamera`, `ThirdPersonCamera`, or camera-orbiting rule — positioned and oriented outside the box so the collider-only wall lies directly between the camera and the box interior.

#### Scenario: Single static camera entity
- **WHEN** the module's entity declarations and rules are inspected
- **THEN** exactly one entity applies both `Camera` and `Viewport`, and no rule in the module writes to that entity's `WorldTransform` on any tick or update phase
