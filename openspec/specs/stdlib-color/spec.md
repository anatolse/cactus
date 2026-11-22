# stdlib-color Specification

## Purpose

Define `std.color`, the stdlib module of pure color helpers written in Cactus: blending two colors, scaling alpha, and the neutral color constants.

## Requirements

### Requirement: std.color.mix blends two colors
`std.color` SHALL provide `pub func mix(a: color, b: color, t: float) color` that returns `a + (b - a) * t`, with no clamp of `t` or of any channel. `mix` SHALL be a pure Cactus function with no extern part, and its body SHALL be GLSL-translatable by the rule stage handlers use, so it works in a render stage with no translation registration.

#### Scenario: Ends
- **WHEN** `mix(a, b, 0.0)` and `mix(a, b, 1.0)` are evaluated
- **THEN** the results are `a` and `b`

#### Scenario: A channel that goes down
- **WHEN** `mix(color(1.0, 1.0, 1.0, 1.0), color(0.0, 0.0, 0.0, 1.0), 0.5)` is evaluated
- **THEN** the result is `color(0.5, 0.5, 0.5, 1.0)`

#### Scenario: Weight outside 0..1
- **WHEN** `mix(color(0.0, 0.0, 0.0, 1.0), color(1.0, 0.0, 0.0, 1.0), 1.5)` is evaluated
- **THEN** the result is `color(1.5, 0.0, 0.0, 1.0)`

#### Scenario: Same value in a render stage
- **WHEN** the same `mix` call is evaluated in a rule handler and in a fragment-stage handler
- **THEN** both give the same channel values

### Requirement: std.color.with_alpha scales alpha
`std.color` SHALL provide `pub func with_alpha(base: color, alpha: float) color` that keeps the RGB channels of `base` and multiplies its alpha by `alpha`, with no clamp. It SHALL be a pure Cactus function, GLSL-translatable like `mix`.

#### Scenario: Half alpha
- **WHEN** `with_alpha(#FF0000FF, 0.5)` is evaluated
- **THEN** the result is `color(1.0, 0.0, 0.0, 0.5)`

#### Scenario: Out-of-range alpha is kept
- **WHEN** `with_alpha(#FF0000FF, 1.5)` is evaluated
- **THEN** the result's alpha is 1.5

### Requirement: Neutral color constants
`std.color` SHALL declare the constants `WHITE = #FFFFFFFF` and `CLEAR = #00000000`.

#### Scenario: Constants
- **WHEN** a program reads `color.WHITE` and `color.CLEAR`
- **THEN** they are `#FFFFFFFF` and `#00000000`
