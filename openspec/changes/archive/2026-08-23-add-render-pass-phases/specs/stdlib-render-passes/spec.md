## Purpose

Define the `std.render.passes` stdlib module: the `Pass`/`Target` compile-time descriptor enums
consumed by `dsl-render-passes`' recognition rule, the fixed built-in per-invocation field set for
the `Quads` pass kind, and the one small pure helper the shipped example needs.

## ADDED Requirements

### Requirement: `std.render.passes` declares the Pass and Target descriptor enums

The module SHALL declare `pub enum Pass` with variant `Quads`, and `pub enum Target` with variant
`Screen`. These are ordinary Cactus enums — no new type-declaration grammar is introduced; their
significance as render-pass descriptors comes entirely from `dsl-render-passes`'s resolved-type
recognition rule, not from anything special about their own declaration.

#### Scenario: Pass and Target are ordinary enum types

- **WHEN** a program imports `std.render.passes` and references `passes.Pass.Quads` or
  `passes.Target.Screen`
- **THEN** these resolve as ordinary enum-variant values with no special syntax

### Requirement: `Quads` pass kind built-in fields

For a render-pass phase whose `Pass` field is `passes.Pass.Quads`, the derived `<phase>.vertex`
trigger alias SHALL expose built-in input fields `corner: vec2`, `uv: vec2`, `vertex_index: int`
(one of 6 fixed values per selected instance, corresponding to two triangles covering corners
`(-1,-1)`, `(1,-1)`, `(1,1)`, `(-1,-1)`, `(1,1)`, `(-1,1)` with UVs `(0,0)`, `(1,0)`, `(1,1)`,
`(0,0)`, `(1,1)`, `(0,1)` respectively) and writable built-in output fields `screen_position:
vec2`, `uv_out: vec2`, `tint_out: color`. The derived `<phase>.fragment` trigger alias SHALL
expose built-in input fields `uv: vec2` and `tint: color` (the vertex stage's
`uv_out`/`tint_out`, linearly interpolated across the triangle in screen space) and `frag_coord:
vec2`, and a writable built-in output field `frag_color: color`. No other fields are available on
either trigger alias for the `Quads` pass kind in this increment.

#### Scenario: Vertex handler writes screen_position from a filtered trait and the built-in corner

- **WHEN** a vertex-stage handler on a `Quads` render pass reads `xf.position` from its `filter:`
  and the built-in `v.corner`
- **THEN** it may assign `v.screen_position = xf.position + v.corner * half_size` (or equivalent)
  and this is accepted

#### Scenario: Fragment handler reads only interpolated built-ins

- **WHEN** a fragment-stage handler on a `Quads` render pass reads `f.uv` and `f.tint`
- **THEN** these resolve to the vertex stage's `uv_out`/`tint_out`, linearly interpolated for the
  current sample

#### Scenario: Output rendered with existing blending

- **WHEN** a fragment handler assigns `f.frag_color`
- **THEN** the backend blends the result onto the render pass's `Target` using the same
  source-over alpha blending already used by existing `std.render.*` rendering

### Requirement: `with_alpha` color helper

`std.render.passes` SHALL provide `pub func with_alpha(base: color, alpha: float) color`, a pure
Cactus function (not `extern`) that preserves `base`'s RGB channels and multiplies its alpha
channel by `clamp(alpha, 0.0, 1.0)`. Being an ordinary pure `func`, it requires no
portable-GLSL-translation registration beyond what its own body already needs — its body is
GLSL-translatable by the same restricted-statement rule stage handlers use
(`dsl-render-passes`, "Stage handler body is restricted...").

#### Scenario: with_alpha scales only the alpha channel

- **WHEN** `with_alpha(#FF0000FF, 0.5)` is evaluated
- **THEN** the result has the same RGB channels as `#FF0000FF` and an alpha channel scaled by
  `0.5`

#### Scenario: with_alpha clamps out-of-range alpha

- **WHEN** `with_alpha(base, 1.5)` or `with_alpha(base, -0.2)` is evaluated
- **THEN** the effective alpha multiplier is clamped to `1.0` or `0.0` respectively
