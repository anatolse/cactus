# stdlib-render-effects Specification

## Purpose

Define the stdlib effect channels: effect entities linked to a target, one small trait per channel, and native Cactus rules that fold all live effects of a target into its drawn pose and drawn color.

## Requirements

### Requirement: Effects are entities linked to a target
`std.render.effects` SHALL declare the marker `pub trait Effect`. `std.render.effects.flat` and `std.render.effects.volume` SHALL each declare `pub relation Affects` from `Effect` to the `WorldTransform` of that flavour. An effect SHALL contribute to a target only while a row `Affects(effect, target)` exists. The modules SHALL contain no extern rule and no extern function.

#### Scenario: Effect starts
- **WHEN** a handler spawns an effect entity and adds `Affects(effect, target)`
- **THEN** from the next render the target's picture includes the effect

#### Scenario: Several effects from one handler
- **WHEN** one handler spawns a jolt and a flash and adds an `Affects` row for each to the same target
- **THEN** the program compiles with no `after:`, and from the next render the target's picture includes both

#### Scenario: Effect ends
- **WHEN** an effect entity is destroyed
- **THEN** from the next render the target's picture no longer includes it, with no rule that resets anything

#### Scenario: Target without effects
- **WHEN** a target has no `Affects` row
- **THEN** it has no `DrawnPose` and no `DrawnColor` from these modules

### Requirement: Channels store start, end and a curve
Each channel trait SHALL have the `var` fields `start`, `end`, `curve: std.math.ease.Curve` (default `Linear`) and `at: float` (default 1.0). The channel's value SHALL be `start + (end - start) * ease.apply(curve, at)`. While the effect entity carries a `std.time.Span`, `at` SHALL equal the span's progress at every render. Without a span, `at` SHALL keep the value last written.

#### Scenario: Eased shift
- **WHEN** an effect has `Shift` with `start = (4, 0)`, `end = (0, 0)`, `curve = OutQuad` and a span at progress 0.5
- **THEN** the channel's value is (1, 0)

#### Scenario: Permanent effect
- **WHEN** an effect has `Tint` with `end = #FFDF60FF` and no span
- **THEN** the channel's value is `#FFDF60FF` for as long as the effect exists

#### Scenario: Game-driven channel
- **WHEN** a game rule writes `shift.end` every tick from a noise function and the effect has no span
- **THEN** the channel's value follows the written value

### Requirement: The standard channels
The modules SHALL declare the channels `Shift` (flat `vec2`, volume `vec3`), `Scale` (flat `vec2`, volume `vec3`, default 1 per axis), `Turn` (flat only, `float` radians), `Tint` (`color`, default white) and `Glow` (`color`, default clear).

#### Scenario: Default scale is neutral
- **WHEN** an effect has `Scale` with no field set
- **THEN** the channel's value is (1, 1) and the target's drawn scale equals its `WorldTransform.scale`

### Requirement: Live pose effects fold into the drawn pose
For each target linked to an effect with a pose channel, the modules SHALL project a `DrawnPose` before any renderer runs: `WorldTransform` position plus the sum of `Shift` values, scale times the per-axis product of `Scale` values, and (flat) rotation plus the sum of `Turn` values. The result SHALL NOT depend on handler schedule order. The modules SHALL NOT write `WorldTransform`.

#### Scenario: Two shifts
- **WHEN** two effects of one target have `Shift` values (3, 0) and (0, 2)
- **THEN** the target's `DrawnPose.position` is its `WorldTransform.position` plus (3, 2)

#### Scenario: Two scales
- **WHEN** two effects of one target have `Scale` values (1.2, 1.2) and (0.9, 0.9)
- **THEN** the target's `DrawnPose.scale` is its `WorldTransform.scale` times (1.08, 1.08) within float rounding

### Requirement: Live color effects fold into the drawn color
For each target with an `Affects` row from an effect that has a color channel, the modules SHALL project a `DrawnColor` before any renderer runs. Its `tint` SHALL be the per-channel product of the effects' `Tint` values and its `glow` the per-channel sum of their `Glow` values, with no clamp. The result SHALL NOT depend on handler schedule order. The modules SHALL NOT write any renderer trait.

#### Scenario: Highlight and fade together
- **WHEN** one effect has `Tint` `#FFFF80FF` and another has `Tint` `#FFFFFF80`
- **THEN** the target's `DrawnColor.tint` is `#FFFF8080` within float rounding, and no ordering between rules was needed

#### Scenario: Glows add past white
- **WHEN** two effects of one target have `Glow` values `#80808000` and `#A0A0A000`
- **THEN** the target's `DrawnColor.glow` channels hold their exact float sum, above 1.0, and only the output clamps

#### Scenario: Simulation untouched
- **WHEN** any number of effects act on a target
- **THEN** its `WorldTransform` and its renderer's color hold the values the game wrote
