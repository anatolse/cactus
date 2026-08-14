# example-bouncy-balls-3d Specification

## Purpose

Define the bouncy-balls-3d Cactus example: a 3D evolution of bouncy-bubbles demonstrating cross-domain and same-domain pair rules for sphere-vs-oriented-box and sphere-vs-sphere collision response generalized to `vec3`/`quat`, a fully enclosed 6-face box with one collider-only face, and mesh-based 3D rendering with per-entity color and point-light illumination.

## Requirements

### Requirement: Bouncy balls 3D Cactus module

The example SHALL provide a single `examples/bouncy-balls-3d/main.cactus` file that is valid Cactus DSL. It SHALL declare a `Ball` trait with only a `velocity: vec3` field, a `SphereCollider` trait with a `radius: float` field, an empty `Wall` marker trait, and a `BoxCollider` trait with a `size: vec3` field. Collider traits SHALL be independent of `std.render.meshes.Renderer` and SHALL NOT duplicate their data onto `Ball` or `Wall`. The module SHALL declare 6 `Wall` entities forming a closed box boundary as static entity declarations. `Ball` entities SHALL NOT be declared statically; instead the module SHALL spawn them at runtime (see "Initial ball batch spawns at load time" and "Click spawns a randomized ball inside the box"), with each spawned ball's `SphereCollider.radius` and initial `velocity` drawn from `std.random` rather than hard-coded per instance.

#### Scenario: Module compiles without errors
- **WHEN** the compiler is invoked with `cactus examples/bouncy-balls-3d/main.cactus --backend cpp-entt -o bouncy-balls-3d.generated.h`
- **THEN** the command exits with code 0 and produces valid generated C++ output

#### Scenario: Eight balls with distinct radii
- **WHEN** the program has completed its initial `load` phase and no click has yet occurred
- **THEN** exactly 8 live entities apply `Ball` and `SphereCollider`, each with a `SphereCollider.radius` drawn independently from the same continuous distribution (not a hard-coded per-entity constant), and no two of them declare the same `SphereCollider.radius` value

#### Scenario: Six walls form a closed box boundary
- **WHEN** the module's entity declarations are inspected
- **THEN** exactly 6 entities apply `Wall` and `BoxCollider`, together enclosing the balls' spawn volume, and at least one of the 6 walls has a `WorldTransform.rotation` that is not a cardinal-axis-aligned orientation

#### Scenario: Collider data is not duplicated on gameplay traits
- **WHEN** the `Ball` and `Wall` trait declarations are inspected
- **THEN** `Ball` declares only `velocity` and `Wall` declares no fields — radius and size live exclusively on `SphereCollider` and `BoxCollider`

### Requirement: Wall and ball entities are template-backed

The example SHALL declare a `template` archetype for walls and a `template` archetype for balls, each bundling the traits and default field values shared by every instance of that kind. Every `Wall` entity SHALL be declared as a template-backed static entity (`entity Name from <Template>:`), overriding only the fields that differ per instance. Every `Ball` entity SHALL instead be instantiated via `spawn <BallTemplate>:` (never a static `entity ... from BallTemplate:` declaration), overriding at minimum `position`, `SphereCollider.radius`, `Ball.velocity`, and render color at each spawn site.

#### Scenario: Wall entities share one template
- **WHEN** the module's wall entity declarations are inspected
- **THEN** all 6 wall entities are declared `from` the same wall template, each overriding only its `position` and `rotation` (and, for the one collider-only wall, omitting the rendering trait it would otherwise inherit)

#### Scenario: Ball entities share one template
- **WHEN** the module's ball-producing rules are inspected
- **THEN** every `spawn` of a ball names the same ball template, and no `Ball` entity is produced via a static `entity ... from` declaration

### Requirement: Initial ball batch spawns at load time

The example SHALL declare a rule that spawns exactly 8 balls from `BallTemplate` in an `on load:` handler, looping over the same 8 corner positions used by the previous static entities. For each spawned ball in the loop, `SphereCollider.radius` SHALL be sampled from a continuous uniform distribution over the same `[0.15, 0.42]` range the previous hard-coded radii spanned, `Ball.velocity` SHALL be a uniformly random 3D direction scaled by an independently sampled speed, and `Renderer.color` SHALL be `std.random.palette_color(k)` where `k` is that ball's loop index (0 through 7) — a deterministic, not randomly sampled, index — so the 8 initial balls always get 8 distinct palette colors.

#### Scenario: Load-time batch runs once at program start
- **WHEN** the program starts (the initial load phase, per `dsl-scene-loading`)
- **THEN** the load-time batch rule's handler executes exactly once and spawns exactly 8 `Ball` entities

#### Scenario: Initial balls occupy the original corner layout
- **WHEN** the 8 load-time-spawned balls' positions are inspected
- **THEN** they match the 8 non-overlapping corner positions the example used before this change, so the starting layout is visually unchanged

#### Scenario: Initial balls are not visually identical across runs
- **WHEN** the shared `Rng` singleton is seeded differently between two runs
- **THEN** the 8 initial balls' radii and velocities differ between those runs (they are sampled, not hard-coded per-entity constants)

### Requirement: Click spawns a randomized ball inside the box

The example SHALL declare an `input SpawnBall: button` bound to the left mouse button, and a rule with an `on input:` handler that, on each press-edge of `SpawnBall`, computes a world-space spawn point by projecting `std.input.mouse_position()` through `std.camera.volume.screen_to_plane` onto the box's `z = 0` plane (`plane_origin = vec3(0,0,0)`, `plane_normal = vec3(0,0,1)`), clamps that point's `x` and `y` components so the new ball's sphere (using its own sampled radius) stays clear of the box's interior wall faces, and spawns one `BallTemplate` there with an independently sampled radius, a uniformly random 3D direction scaled by an independently sampled speed, and a color from `std.random.palette_color` at a randomly sampled index. The example SHALL impose no upper bound on the number of balls spawned this way.

#### Scenario: A single press spawns exactly one ball
- **WHEN** `SpawnBall` transitions from not-pressed to pressed on a given frame
- **THEN** exactly one new `Ball` entity is spawned that frame, positioned at the mouse's projected point on the `z = 0` plane

#### Scenario: Holding the button does not spawn every frame
- **WHEN** `SpawnBall` remains held down across multiple consecutive frames after its initial press
- **THEN** no additional ball is spawned for those held-down frames

#### Scenario: Spawn position is clamped clear of the walls
- **WHEN** the mouse's projected point lies at or beyond a wall's inner face
- **THEN** the spawned ball's position is clamped so its sphere does not start overlapping that wall

#### Scenario: Ball count is unbounded
- **WHEN** `SpawnBall` has already been pressed enough times that the total ball count exceeds the original 8
- **THEN** the next press still spawns another ball; no existing ball is destroyed or spawn rejected to enforce a cap

### Requirement: Spawned ball randomization draws from one shared, persistent RNG

The example SHALL declare a singleton entity carrying a `var rng: std.random.Rng` field (seeded once via `std.random.seeded`). Every draw used to spawn a ball — whether from the load-time batch or a click — SHALL read this field, advance it via `std.random.advance`, sample from it, and write the advanced state back to the same field, so the generator state persists across both the load-time loop and every subsequent click.

#### Scenario: Successive draws within the load-time loop differ
- **WHEN** the load-time batch rule spawns its 8 balls in sequence
- **THEN** each iteration's sampled values are drawn from a distinct `Rng` state (the state advanced by the previous iteration), not the same fixed draw repeated 8 times

#### Scenario: RNG state carries from the load-time batch into click spawns
- **WHEN** a click spawns a ball after the load-time batch has already run
- **THEN** the click-spawn draw uses the `Rng` state as last advanced by the load-time batch, not a freshly re-seeded generator

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

The example SHALL apply `std.render.meshes.Renderer` to each `Ball` entity, using a `mesh` asset whose declared path resolves to placeholder sphere geometry, and to 5 of the 6 `Wall` entities, using a `mesh` asset that resolves to placeholder box geometry. Each `Ball` entity's `Renderer.color` SHALL be set at spawn time via `std.random.palette_color`. Each entity's `tv.WorldTransform.scale` SHALL correspond to its own collider dimensions: a `Ball` entity's scale SHALL equal `vec3(radius, radius, radius)` where `radius` is that same entity's `SphereCollider.radius`, and a `Wall` entity's scale SHALL equal that same entity's `BoxCollider.size` exactly. The sixth `Wall` entity (the one the camera looks through) SHALL declare no `std.render.meshes.Renderer` (or any other rendering trait) at all.

#### Scenario: Ball mesh asset resolves to sphere geometry
- **WHEN** a `Ball` entity's `Renderer.mesh` asset declaration is inspected
- **THEN** its declared path contains the substring `sphere`

#### Scenario: Ball entities have distinct colors
- **WHEN** the 8 balls spawned by the load-time batch (see "Initial ball batch spawns at load time") are inspected immediately after the `load` phase
- **THEN** no two of them declare the same `Renderer.color` value

#### Scenario: Click-spawned balls are not guaranteed distinct from existing balls
- **WHEN** a ball is spawned via a click after the initial batch
- **THEN** its `Renderer.color` is drawn from the same fixed palette as every other ball via a randomly sampled index, and MAY coincide with an existing ball's color

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
- **WHEN** the 8 balls spawned by the load-time batch have their `tv.WorldTransform.scale` values inspected
- **THEN** no two of them declare the same scale value, matching their already-distinct `SphereCollider.radius` values

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
