## ADDED Requirements

### Requirement: cpp-entt lowers Quads render passes to a generated raylib shader pair

For a render-pass phase whose `Pass` field is `passes.Pass.Quads`, the `cpp-entt` backend SHALL
generate one GLSL vertex shader and one GLSL fragment shader translated directly from the phase's
vertex- and fragment-stage handler bodies (per the restricted statement/expression subset in
`dsl-render-passes`), load them via raylib's `LoadShaderFromMemory`, and draw one instanced quad
per entity selected by the vertex handler's `filter:` using `BeginShaderMode`/raylib's existing
draw-call surface. The generated draw step SHALL participate in the existing `effects: graphics`
scheduling domain (per `handler-execution-graph`, "Contract conflict edges") the same way
`SpriteRenderer`/`MeshRenderer` already do.

#### Scenario: Vertex/fragment handler bodies translate to GLSL

- **WHEN** `cpp-entt` compiles a `Quads` render-pass phase with vertex handler `V` and fragment
  handler `F`
- **THEN** the generated output contains one GLSL vertex shader derived from `V`'s body and one
  GLSL fragment shader derived from `F`'s body, loaded via raylib at program startup

#### Scenario: Per-instance draw uses the selected entity domain

- **WHEN** the vertex handler's `filter:` selects entities carrying `WorldTransform` and
  `Particle`
- **THEN** the generated draw step issues one instanced quad per currently-matching entity each
  time the render-pass phase activates

#### Scenario: Render-pass draw step shares graphics-effect ordering with existing renderers

- **WHEN** a program uses both a `Quads` render pass and an existing
  `std.render.shapes.ShapeRenderer` extern rule
- **THEN** their relative draw order is resolved the same deterministic way existing `effects:
  graphics` conflicts are resolved (declaration order, absent explicit ordering)

### Requirement: Portable GLSL translation is an explicit per-function registration

An `extern func` or stdlib `func` is only callable from a stage handler body if the `cpp-entt`
backend has a registered GLSL translation for its canonical symbol. `std.math.sqrt`,
`std.math.clamp`, and `std.render.passes.with_alpha` SHALL be registered for this increment; no
other function is assumed portable merely because its signature uses supported types.

#### Scenario: Registered intrinsic is callable from a stage handler

- **WHEN** a fragment-stage handler calls `math.sqrt(radius_squared)`
- **THEN** compilation succeeds and the generated fragment shader calls GLSL's built-in `sqrt`

#### Scenario: Unregistered function is rejected

- **WHEN** a stage handler body calls a stdlib or extern function with no registered GLSL
  translation
- **THEN** compilation fails with a diagnostic naming the call (per `dsl-render-passes`, "Stage
  handler body is restricted...")
