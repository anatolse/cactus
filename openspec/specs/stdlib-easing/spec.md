# stdlib-easing Specification

## Purpose

Define `std.math.ease`, the stdlib module of easing curves: pure functions that turn progress 0..1 into a shaped 0..1 value, and an enum that names them so data can choose a curve.

## Requirements

### Requirement: std.math.ease provides named easing curves
`std.math.ease` SHALL declare `pub enum Curve` with the variants `Linear`, `InQuad`, `OutQuad`, `InOutQuad`, `InCubic`, `OutCubic`, `InOutCubic`, `InBack`, `OutBack` and `OutBounce`, and one `pub func` per variant taking and returning a `float`: `linear`, `in_quad`, `out_quad`, `in_out_quad`, `in_cubic`, `out_cubic`, `in_out_cubic`, `in_back`, `out_back`, `out_bounce`. Each SHALL be a pure Cactus function with no `extern func` call.

#### Scenario: Ease out is ahead of linear
- **WHEN** `out_quad(0.5)` and `in_quad(0.5)` are evaluated
- **THEN** the results are 0.75 and 0.25

#### Scenario: In-out curves pass the middle
- **WHEN** `in_out_quad(0.5)` and `in_out_cubic(0.5)` are evaluated
- **THEN** both results are 0.5

#### Scenario: Back curves overshoot
- **WHEN** `out_back` and `in_back` are evaluated over 0..1
- **THEN** some `out_back` value is above 1.0 before the end and some `in_back` value is below 0.0 after the start

#### Scenario: Bounce stays in range
- **WHEN** `out_bounce` is evaluated over 0..1
- **THEN** every value is inside 0.0..1.0

### Requirement: Easing input is clamped and ends are exact
Each curve function SHALL treat an input below 0.0 or NaN as 0.0 and an input above 1.0 as 1.0, and SHALL return exactly 0.0 for input 0.0 and exactly 1.0 for input 1.0.

#### Scenario: Ends are exact
- **WHEN** any curve function is called with 0.0 and with 1.0
- **THEN** it returns exactly 0.0 and exactly 1.0

#### Scenario: Input out of range
- **WHEN** `out_quad(1.7)`, `out_quad(-0.3)` and `out_back(0.0 / 0.0)` are evaluated
- **THEN** the results are 1.0, 0.0 and 0.0

### Requirement: A curve can be chosen by data
`std.math.ease` SHALL provide `pub func apply(curve: Curve, t: float) float` that returns the value of the function named by `curve`. `Curve` SHALL be usable as a trait field type (including a `persist` field), an event field type and a template argument.

#### Scenario: Apply matches the function
- **WHEN** `apply(c, t)` is evaluated for every variant `c` and `t` in 0.0, 0.25, 0.4, 0.75 and 1.0
- **THEN** each result equals the variant's own function at `t`

#### Scenario: Curve as data
- **WHEN** a trait declares `persist var curve: ease.Curve = ease.Curve.Linear`, a template argument sets it, an event field carries a new curve that a handler stores, and a rule computes `color.mix(a, b, ease.apply(f.curve, time.progress(s)))`
- **THEN** the program compiles and each entity uses the curve stored on it
