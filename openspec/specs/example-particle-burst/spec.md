# example-particle-burst Specification

## Purpose

Define the particle-burst Cactus example: a self-contained module demonstrating mouse-triggered
particle spawning, gravity/lifetime simulation, and circle-shape rendering using only ordinary
`spawn`/`destroy` entities and the existing `std.render.shapes` rendering trait — no fixed-capacity
pool, no device-placement inference, no new stdlib or rendering constructs.

## Requirements

### Requirement: Particle burst example Cactus module

The example SHALL provide a single `examples/particle-burst/particle_burst.cactus` file that is
valid Cactus DSL. It SHALL import `std.input`, `std.render.shapes`, and `std.transform.flat`. It
SHALL declare a `Particle` trait with `velocity: vec2` and `lifetime: float` fields, and a
`template` that combines `std.transform.flat.WorldTransform`, `std.render.shapes.Shape`, and
`Particle` for use as the burst-member shape.

#### Scenario: Module compiles without errors

- **WHEN** the compiler is invoked with `cactus examples/particle-burst/particle_burst.cactus --backend cpp-entt -o particle-burst.generated.h`
- **THEN** the command exits with code 0 and produces valid generated C++ output

### Requirement: Left mouse click spawns a fixed-size burst of particles

The example SHALL declare an `input` binding for the left mouse button and a rule triggered
`on input:` that, when that input is pressed, spawns a fixed, compile-time-known number of
particle entities at the current mouse position. Each burst member SHALL be created by its own
`spawn` statement with a distinct initial velocity; the example SHALL NOT use a numeric loop or
list iteration to produce the burst, since the language provides neither.

#### Scenario: Burst spawns a fixed count of particles at the cursor

- **WHEN** the bound mouse button transitions to pressed during an `input` activation
- **THEN** the rule spawns the example's fixed burst count of particle entities
- **AND** each spawned entity's `WorldTransform.position` equals the mouse position read during that activation
- **AND** no two spawned entities in the same burst share the same initial `velocity`

#### Scenario: Repeated clicks spawn independent bursts

- **WHEN** the bound mouse button is pressed again while particles from an earlier burst are still alive
- **THEN** a new set of particle entities is spawned in addition to the still-alive particles
- **AND** the example enforces no cap on the number of concurrently alive particle entities

### Requirement: Particles fall under gravity and expire after their lifetime

The example SHALL declare a rule, filtered on `Particle` and `WorldTransform`, triggered
`on fixed_tick:`, that applies a constant downward gravity to `velocity`, integrates
`WorldTransform.position` using the updated `velocity` (semi-implicit Euler: velocity is updated
before it is used to move position), and decrements `lifetime` by the fixed step size. When a
particle's `lifetime` reaches zero or below, the rule SHALL `destroy` that entity.

#### Scenario: Gravity and position integration apply each fixed step

- **WHEN** a fixed-tick step of size `dt` runs for a live particle entity
- **THEN** `velocity.y` increases by `gravity * dt` before `WorldTransform.position` is updated
- **AND** `WorldTransform.position` is updated using the post-gravity `velocity`

#### Scenario: Particle is destroyed once its lifetime elapses

- **WHEN** a live particle entity's `lifetime` reaches zero or below during a fixed-tick step
- **THEN** that entity is destroyed and no longer receives further simulation or rendering

### Requirement: Particles render as circles via existing shape rendering

Every spawned particle entity SHALL carry `std.render.shapes.Shape` with `type = ShapeType.Circle`
alongside its `std.transform.flat.WorldTransform`, so the existing, unmodified `ShapeRenderer`
extern rule renders it. The example SHALL NOT introduce any new rendering trait, extern rule, or
stdlib module.

#### Scenario: Spawned particle has a Circle shape

- **WHEN** a particle entity is spawned by the burst rule
- **THEN** its `Shape.type` is `ShapeType.Circle`
- **AND** it carries a `WorldTransform` component with no `LocalTransform` or hierarchy propagation involved
