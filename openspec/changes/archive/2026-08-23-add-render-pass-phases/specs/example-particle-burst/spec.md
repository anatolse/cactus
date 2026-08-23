## Purpose

Define the particle-burst Cactus example: a self-contained module demonstrating mouse-triggered
particle spawning, gravity/lifetime simulation, and soft-circle rendering via an authored
`dsl-render-passes` render pass, using ordinary `spawn`/`destroy` entities — no fixed-capacity
pool, no device-placement inference or choice (the render pass has exactly one lowering path, per
`add-render-pass-phases`), no change to spawn/simulation mechanics.

## MODIFIED Requirements

### Requirement: Particle burst example Cactus module

The example SHALL provide a single `examples/particle-burst/particle_burst.cactus` file that is
valid Cactus DSL. It SHALL import `std.input`, `std.render.passes`, `std.math`, and
`std.transform.flat`. It SHALL declare a `Particle` trait with `velocity: vec2` and `lifetime:
float` fields, and a `template` that combines `std.transform.flat.WorldTransform` and `Particle`
(no `std.render.shapes.Shape`) for use as the burst-member shape. It SHALL declare a render-pass
phase (`particle_pass`, `after: render`, `pipeline: passes.Pass = passes.Pass.Quads`, `output:
passes.Target = passes.Target.Screen`) and its vertex/fragment stage handlers.

#### Scenario: Module compiles without errors

- **WHEN** the compiler is invoked with `cactus examples/particle-burst/particle_burst.cactus
  --backend cpp-entt -o particle-burst.generated.h`
- **THEN** the command exits with code 0 and produces valid generated C++ output, including the
  generated GLSL shader pair for `particle_pass`

## REMOVED Requirements

### Requirement: Particles render as circles via existing shape rendering

**Reason**: Superseded by an authored render pass producing a soft radially-faded look, per
`add-render-pass-phases`. The prior flat-circle rendering via `std.render.shapes.Shape`/
`ShapeType.Circle` was this example's own design.md's explicitly-named deferred next step
("Revisit only if a future iteration reintroduces custom vertex/fragment rendering.").

**Migration**: Remove `std.render.shapes.Shape` from `ParticleTemplate`; add `particle_pass` and
its `ParticleVertex`/`ParticleFragment` stage handlers per `add-render-pass-phases/design.md`
Decision 5.

## ADDED Requirements

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
