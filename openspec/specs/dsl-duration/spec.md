## Purpose

Give the language an exact time type: a whole number of one fixed time unit, written with unit literals, so time arithmetic never rounds and the meaning of a time value is written in the program.

## Requirements

### Requirement: A duration counts a fixed time unit
`duration` SHALL be a built-in type whose value is a count of one fixed unit, 1/705,600,000 of a second, in the ring ℤ/2⁶⁴ read as signed. The type SHALL have no parameter. The unit SHALL be the same in every program and build.

#### Scenario: A field of duration type
- **WHEN** a trait declares `var cooldown: duration = 0s`
- **THEN** compilation accepts it and `cooldown` holds zero units

#### Scenario: A millisecond is a whole number of units
- **WHEN** a constant is declared as `1ms`
- **THEN** its value is 705,600 units

### Requirement: Duration literals carry a unit and are exact
A number literal followed directly by `ms`, `s`, `min` or `h` SHALL be a duration literal. The number SHALL be read as an exact decimal and multiplied by the unit's size without any `float` step. A literal whose value is not a whole number of units, or is outside the signed 64-bit range, SHALL be a compile error that names the nearest values that fit. A number and a suffix separated by a space SHALL NOT form a literal.

#### Scenario: Literals with each suffix
- **WHEN** a program writes `5s`, `120ms`, `3min`, `2h` and `0.5s`
- **THEN** their values are 3,528,000,000, 84,672,000, 127,008,000,000, 5,080,320,000,000 and 352,800,000 units

#### Scenario: A literal that is not a whole number of units
- **WHEN** a program writes `0.001ms`
- **THEN** the compiler reports an error at the literal and names the nearest values that fit

#### Scenario: A positive literal is never zero
- **WHEN** any duration literal with a non-zero number compiles
- **THEN** its value is not zero

### Requirement: Floats convert only by an explicit total function
A `float` expression, constant or not, where a duration is expected SHALL be a type error that suggests a duration literal or `duration(...)`. `duration(x)` SHALL convert a `float` number of seconds: NaN SHALL give zero; any other value SHALL give the integer nearest to `x × 705,600,000` computed in double precision, halves rounded away from zero, clamped to the signed 64-bit range. `seconds(d)` SHALL give `d`'s count divided by 705,600,000, computed in double precision and rounded once to binary32.

#### Scenario: A float constant is rejected
- **WHEN** a handler assigns `0.5` to a duration field
- **THEN** the compiler reports a type error that suggests `0.5s`

#### Scenario: A run-time float converts explicitly
- **WHEN** a handler assigns `duration(x)` with `x` holding `0.25`
- **THEN** the field holds 176,400,000 units

#### Scenario: NaN converts to zero
- **WHEN** `duration(x)` runs with `x` NaN
- **THEN** the result is zero

### Requirement: Phases give the step as a duration
A periodic phase SHALL synthesize a field `delta` of type `duration` next to `dt`. Its `every:` SHALL accept a constant of type `duration` or `float`. With a duration period, `delta` SHALL be that constant and `dt` SHALL be `seconds(delta)`. With a `float` period, `delta` SHALL be the constant `duration(every)`. A phase without `every:` SHALL synthesize no `delta`; it MAY declare one as a phase field. `std.core.frame` SHALL declare `delta: duration`, and `std.core.tick` and `std.core.late_tick` SHALL declare `delta: duration = frame.delta`. `dt` SHALL stay a `float`. `std.core.fixed_tick` SHALL declare `every: 1s / 60`.

#### Scenario: A 60 Hz step
- **WHEN** a phase has `every: 1s / 60`
- **THEN** `step.delta` is 11,760,000 units in every activation
- **AND** `step.dt` is the binary32 value nearest 1/60

#### Scenario: A countdown ends on an exact tick
- **WHEN** a `fixed_tick` handler at 60 Hz runs `remaining -= step.delta` on a field that starts at `100ms`
- **THEN** the field is zero after exactly 6 activations

#### Scenario: A day of steps does not drift
- **WHEN** a `fixed_tick` handler at 60 Hz adds `step.delta` to a duration 5,184,000 times
- **THEN** the field equals `24h`

#### Scenario: A timer on the variable tick
- **WHEN** a `tick` handler runs `remaining -= step.delta` and the host frame has `dt` 0.25
- **THEN** the field loses 176,400,000 units in that activation

#### Scenario: dt in a duration expression
- **WHEN** a handler runs `remaining -= step.dt` on a duration field
- **THEN** the compiler reports a type error that suggests `step.delta`

### Requirement: Duration operations are exact
Adding, subtracting and negating durations, multiplying a duration by an `int`, and `math.min`/`math.max` of two durations SHALL give a duration, wrapping in ℤ/2⁶⁴. Dividing a duration by an `int` SHALL give a duration, the floor of the exact quotient, and zero when the divisor is zero, as `int` division does. Comparing two durations SHALL give `bool`. Dividing two durations SHALL give a `float`: their counts' ratio rounded once to binary32, and 0 when the divisor is zero. Multiplying or dividing a duration by a `float` SHALL be a type error that suggests `duration(seconds(d) * k)`. `%` with a duration operand SHALL be a type error. `d += e` and `d -= e` SHALL be Accum writes; `d = math.max(d, e)` and `d = math.min(d, e)` SHALL be Join writes.

#### Scenario: Two handlers count down one timer
- **WHEN** two unordered `fixed_tick` handlers both run `t -= step.delta` on one duration
- **THEN** no schedule edge is added between them and the field loses two steps

#### Scenario: Progress as a ratio
- **WHEN** a handler divides an elapsed duration of `500ms` by a total of `2s`
- **THEN** the result is `0.25`

#### Scenario: Dividing by an int
- **WHEN** a constant is declared as `1s / 60`
- **THEN** its value is 11,760,000 units

#### Scenario: Dividing a negative duration rounds down
- **WHEN** a handler divides a duration of -7 units by `2`
- **THEN** the result is -4 units

#### Scenario: Scaling by a float
- **WHEN** a handler writes `cooldown * 0.5`
- **THEN** the compiler reports a type error that suggests `duration(seconds(cooldown) * 0.5)`

### Requirement: Stage handlers reject durations
A stage handler of a drawing rule SHALL NOT read, write or declare a value of duration type, whether it comes from a constant, a local, a row field, a named entity or a link; each use SHALL be a source-located error that suggests converting with `seconds(...)` before the stage.

#### Scenario: Duration in a stage handler
- **WHEN** a stage handler reads a `duration` field
- **THEN** the compiler reports an error at the read that suggests `seconds(...)`
