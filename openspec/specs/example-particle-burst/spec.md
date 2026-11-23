# example-particle-burst Specification

## Purpose

Define the particle-burst Cactus example: a self-contained module demonstrating mouse-triggered
particle spawning, gravity/lifetime simulation, and soft round particles drawn by one drawing rule
(`dsl-drawing-rules`), using only ordinary `spawn`/`destroy` entities — no fixed-capacity pool and
no device-placement inference.

## Requirements

### Requirement: Left mouse click spawns a fixed-size burst of particles

The example SHALL declare an `input` binding for the left mouse button and a rule triggered
`on input:` that, when that input is pressed, spawns a fixed, compile-time-known number of
particle entities at the current mouse position. The burst SHALL be produced by a single `spawn`
statement inside a `for k in range(0, PARTICLE_COUNT):` loop, with each particle's initial
velocity computed from the loop index `k` so that no two members of the same burst share the same
initial velocity.

#### Scenario: Burst spawns a fixed count of particles at the cursor

- **WHEN** the bound mouse button transitions to pressed during an `input` activation
- **THEN** the rule spawns the example's fixed burst count of particle entities
- **AND** each spawned entity's `WorldTransform.position` equals the mouse position read during that activation
- **AND** no two spawned entities in the same burst share the same initial `velocity`

#### Scenario: Repeated clicks spawn independent bursts

- **WHEN** the bound mouse button is pressed again while particles from an earlier burst are still alive
- **THEN** a new set of particle entities is spawned in addition to the still-alive particles
- **AND** the example enforces no cap on the number of concurrently alive particle entities

#### Scenario: Particle velocities are evenly distributed around a circle

- **WHEN** the burst rule spawns its fixed count of particles
- **THEN** each particle's initial velocity direction is computed as an angle proportional to its
  loop index, so the burst's velocities are evenly spaced around a full circle

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

### Requirement: Particle burst example module with a drawing rule
`examples/particle-burst/particle_burst.cactus` SHALL be a valid Cactus module that imports `std.input`, `std.math`, `std.color`, `std.render.layers` and `std.transform.flat`, declares a `Particle` trait and a particle template with no `Shape`, and draws particles with one drawing rule. It SHALL declare no phase.

#### Scenario: Module compiles
- **WHEN** the example is compiled with the cpp-entt backend
- **THEN** compilation succeeds and the generated code holds one shader pair for the drawing rule

### Requirement: Particles render as soft circles via a drawing rule
Every particle SHALL be drawn by one drawing rule over `WorldTransform` and `Particle` in `group: layers.sprites`. Its vertex handler SHALL place the quad in world space from `WorldTransform.position` and a fixed radius. Its fragment handler SHALL give a transparent color beyond UV distance 0.5 from the center and an alpha that falls linearly from the center to that edge, using `std.color.with_alpha`.

#### Scenario: Spawned particle is drawn
- **WHEN** a particle is spawned by the burst rule
- **THEN** the drawing rule draws one quad for it

#### Scenario: Soft falloff
- **WHEN** the fragment handler evaluates a sample at UV distance `d` from the center
- **THEN** the sample is transparent when `d > 0.5` and its alpha falls linearly from `d = 0` to `d = 0.5` otherwise

#### Scenario: Destroyed particle
- **WHEN** a particle is destroyed
- **THEN** no quad is drawn for it
