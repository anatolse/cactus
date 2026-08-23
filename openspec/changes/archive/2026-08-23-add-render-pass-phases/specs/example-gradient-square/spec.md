## Purpose

Define the gradient-square Cactus example: a minimal, static demonstration of vertex-to-fragment
color interpolation on a `dsl-render-passes` `Quads` render pass — one static entity, no
`spawn`/`input`, four fixed per-corner colors written by the vertex handler and passed straight
through by the fragment handler, so the entire visible gradient comes from the backend's
rasterizer interpolation rather than any authored per-pixel formula. Companion to
`example-particle-burst`, which never exercises per-vertex color variation (`tint_out` is uniform
per particle instance there).

## ADDED Requirements

### Requirement: Gradient square example Cactus module

The example SHALL provide a single `examples/gradient-square/gradient_square.cactus` file that is
valid Cactus DSL. It SHALL import `std.render.passes` and `std.transform.flat`. It SHALL declare
one static `pub entity` with a `std.transform.flat.WorldTransform` and no other trait. It SHALL
declare a render-pass phase (`after: render`, `pipeline: passes.Pass = passes.Pass.Quads`,
`output: passes.Target = passes.Target.Screen`) and its vertex/fragment stage handlers. It SHALL
NOT declare `spawn`, `input`, or any simulation rule.

#### Scenario: Module compiles without errors

- **WHEN** the compiler is invoked with `cactus examples/gradient-square/gradient_square.cactus
  --backend cpp-entt -o gradient-square.generated.h`
- **THEN** the command exits with code 0 and produces valid generated C++ output, including the
  generated GLSL shader pair for the render pass

### Requirement: Square renders with a distinct color per corner

The vertex-stage handler SHALL filter on `WorldTransform` only, compute the instance's
screen-space quad corner from `WorldTransform.position` and a fixed half-size constant, and write
one of four fixed, distinct colors to `tint_out` based on the built-in `corner` field's sign (one
color per `(±1, ±1)` corner combination). The fragment-stage handler SHALL be selectionless and
assign its `frag_color` output directly from the interpolated `tint` input, with no other
computation.

#### Scenario: Each corner gets a distinct authored color

- **WHEN** the vertex-stage handler runs across a given instance's six fixed corner values
- **THEN** each of the four distinct `(corner.x, corner.y)` sign combinations produces a different
  `tint_out` value

#### Scenario: Fragment output is the interpolated vertex color, unmodified

- **WHEN** the fragment-stage handler evaluates a sample
- **THEN** `frag_color` equals that sample's interpolated `tint` input with no additional
  transformation, so the visible gradient is produced entirely by rasterizer interpolation
