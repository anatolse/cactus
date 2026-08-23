# example-particle-burst Specification

## Purpose

Define the particle-burst Cactus example: a self-contained module demonstrating mouse-triggered
particle spawning, gravity/lifetime simulation, and circle-shape rendering using only ordinary
`spawn`/`destroy` entities and the existing `std.render.shapes` rendering trait — no fixed-capacity
pool, no device-placement inference, no new stdlib or rendering constructs.

## Requirements

### Requirement: Particle burst example Cactus module

The example SHALL provide a single `examples/particle-burst/particle_burst.cactus` file that is
valid Cactus DSL. It SHALL import `std.input`, `std.render.passes`, `std.math`, and
`std.transform.flat`. It SHALL declare a `Particle` trait with `velocity: vec2` and `lifetime:
float` fields, and a `template` that combines `std.transform.flat.WorldTransform` and `Particle`
(no `std.render.shapes.Shape`) for use as the burst-member shape. It SHALL declare a render-pass
phase (`particle_pass`, `after: render`, `pipeline: passes.Pass = passes.Pass.Quads`, `output:
passes.Target = passes.Target.Screen`) and its vertex/fragment stage handlers.

#### Scenario: Module compiles without errors

- **WHEN** the compiler is invoked with `cactus examples/particle-burst/particle_burst.cactus --backend cpp-entt -o particle-burst.generated.h`
- **THEN** the command exits with code 0 and produces valid generated C++ output, including the
  generated GLSL shader pair for `particle_pass`

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

### Requirement: Particles render as soft circles via an authored render pass

Every spawned particle entity SHALL be rendered by `particle_pass`, a `Quads`-kind render-pass
phase declared in the example module. The vertex-stage handler SHALL filter on `WorldTransform`
and `Particle`, compute each instance's screen-space quad corner from `WorldTransform.position`
and a fixed particle radius, and pass through UV and a fixed tint. The fragment-stage handler
SHALL produce a transparent color outside a centered radius of `0.5` in UV space and linearly fade
alpha from center to edge inside it, using `std.render.passes.with_alpha`. The example SHALL NOT
introduce any new rendering trait, extern rule, or stdlib module beyond `std.render.passes`
(already added by `add-render-pass-phases`).

#### Scenario: Spawned particle is instanced by the render pass

- **WHEN** a particle entity is spawned by the burst rule
- **THEN** `particle_pass`'s vertex-stage handler selects it (via its `WorldTransform`+`Particle`
  filter) and it is instanced as a quad

#### Scenario: Fragment produces a soft circular falloff

- **WHEN** `particle_pass`'s fragment-stage handler evaluates a sample at UV distance `d` from
  the quad center
- **THEN** the sample is fully transparent when `d > 0.5` and its alpha linearly decreases from
  the center (`d = 0`) to the edge (`d = 0.5`) otherwise

#### Scenario: Destroyed particles are no longer instanced

- **WHEN** a particle entity is destroyed by the simulation rule (lifetime elapsed)
- **THEN** it is no longer selected by the vertex handler's filter and no longer contributes an
  instance to the render pass
