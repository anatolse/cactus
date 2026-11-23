# example-gradient-square Specification

## Purpose

Define the gradient-square Cactus example: a minimal, static demonstration of vertex-to-fragment
color interpolation in a drawing rule (`dsl-drawing-rules`) — one static entity, no
`spawn`/`input`, four fixed per-corner colors written by the vertex handler and passed straight
through by the fragment handler, so the entire visible gradient comes from the backend's
rasterizer interpolation rather than any authored per-pixel formula. Companion to
`example-particle-burst`, which never exercises per-vertex color variation (`tint_out` is uniform
per particle there).

## Requirements

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

### Requirement: Gradient square example module with a drawing rule
`examples/gradient-square/gradient_square.cactus` SHALL be a valid Cactus module that imports `std.transform.flat` and `std.render.layers`, declares one static entity with a `WorldTransform`, and draws it with one drawing rule whose vertex handler writes a fixed color per corner into `tint_out` and whose fragment handler outputs the interpolated `tint`. It SHALL declare no phase.

#### Scenario: Module compiles
- **WHEN** the example is compiled with the cpp-entt backend
- **THEN** compilation succeeds and the generated code holds one shader pair for the drawing rule

#### Scenario: Corner colors still interpolate
- **WHEN** the square is drawn
- **THEN** each corner shows its own color and the colors blend across the quad, as before
