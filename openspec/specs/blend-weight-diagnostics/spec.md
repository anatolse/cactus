# blend-weight-diagnostics Specification

## Purpose

Define which field writes the compiler treats as a blend toward a goal, how it derives the range of the blend weight, and when it warns that the weight can leave `[0, 1]`. Diagnostics describe a risk; they never change behavior.

## Requirements

### Requirement: A blend is a write that moves a field toward a goal by a weight
A *blend* SHALL be a statement in a rule handler that writes a field `x` of type `float`, `vec2` or `vec3` in one of these forms, where `goal` is any expression and the *weight* `w` has type `float`:

1. `x += (goal - x) * w` or `x -= (x - goal) * w`.
2. `x = x + (goal - x) * w` or `x = x - (x - goal) * w`.
3. `x = lerp(x, goal, w)`, where `lerp` is the `lerp` of `std.math`, `std.math.vec2` or `std.math.vec3`.

In forms 1 and 2 the difference and the other factors MAY be written in any order, and the weight SHALL be the product of the other factors, where a division by `e` counts as the factor `1 / e`. A difference with no other factor SHALL have the weight `1.0`. Forms 2 and 3 SHALL also count when they are an arm of an `if` expression assigned to `x`. The two reads of `x` SHALL be the same access as the write: the same binding and the same field, and for a cell field accessed by `e[T][k1, ..., kn].field` also the same carrier and index expressions written alike. A cell field through a cell alias (`c.heat`) SHALL count as an ordinary field of that alias. A statement whose remaining factor is a vector, or that writes an `int` or `duration` field, SHALL NOT be a blend.

#### Scenario: Accumulating form
- **WHEN** a handler runs `cam.offset += (goal - cam.offset) * blend`
- **THEN** the statement is a blend of `cam.offset` with the weight `blend`

#### Scenario: The goal is a sum
- **WHEN** a handler runs `pose.position += (aim + follow.offset - pose.position) * k`
- **THEN** the statement is a blend with the goal `aim + follow.offset` and the weight `k`

#### Scenario: Weight written first and divided
- **WHEN** a handler runs `u.speed += RATE * (wanted - u.speed) / 4.0`
- **THEN** the statement is a blend with the weight `RATE / 4.0`

#### Scenario: Lerp form
- **WHEN** a handler runs `tf.position = v3.lerp(tf.position, goal, k)`
- **THEN** the statement is a blend with the weight `k`

#### Scenario: A blend inside an if expression
- **WHEN** a handler runs `rig.yaw = if near: goal else: rig.yaw + (goal - rig.yaw) * k`
- **THEN** the `else` arm is a blend with the weight `k`

#### Scenario: A cell rule blends its own cell
- **WHEN** a cell rule over `Heat as c` runs `c.value += (AMBIENT - c.value) * k`
- **THEN** the statement is a blend of `c.value` with the weight `k`

#### Scenario: Spreading through a sum is not a blend
- **WHEN** a cell pair rule declares `reduce per here: net = sum((there.gas - here.gas) * 0.3)` and its handler runs `here.gas += net`
- **THEN** the statement is not a blend and the check reports nothing

#### Scenario: Integration is not a blend
- **WHEN** a handler runs `tf.position += body.velocity * tick.dt`
- **THEN** the statement is not a blend and the check reports nothing

#### Scenario: A vector weight is not a blend
- **WHEN** a handler runs `tf.position += (goal - tf.position) * scale` and `scale` is a `vec3`
- **THEN** the statement is not a blend and the check reports nothing

### Requirement: The weight range is derived from what the program states
The compiler SHALL derive, for every blend, a lower and an upper bound of its weight, where either side MAY be unknown. The bounds SHALL describe every value of the weight that is not NaN. The compiler SHALL derive ranges of `float` expressions and, counted in duration units, of `duration` expressions, and they SHALL come only from these sources:

- A literal, a constant, or any constant expression: its value.
- `dt` and `delta` of a phase with `every:`: the interval. A field of a phase without `every:`: the range of its initializer. The root frame's `dt`: at least `0.0`, with no upper bound. The root frame's `delta`: at least `0s`, with no upper bound. `alpha` of a periodic phase: from `0.0` to `1.0`.
- An `int` `let` field declared `in range(a, b)` with constant bounds: that range.
- A read of a `clamped` field: on each side, the range of the bound on that side, where a constant bound gives its value, a `let` field bound gives the range of that field, and a missing side is unknown.
- `+`, `-`, `*`, `/` and unary minus on `float`: interval arithmetic on the operand ranges, where a division whose divisor range contains zero is unknown on both sides.
- `+`, `-`, unary minus, `* int` and `/ int` on `duration`: interval arithmetic on unit counts, where a side whose exact result can leave the signed 64-bit range is unknown and a division whose divisor range contains zero is unknown on both sides.
- `seconds(d)`: the range of `d` in seconds. `d1 / d2` of two durations: the interval ratio, unknown on both sides when the range of `d2` contains zero.
- `math.min`, `math.max`, `math.clamp` and `math.abs`: the range their definition gives, so `math.min(1.0, e)` is at most `1.0` for any `e`, and `math.clamp(e, a, b)` lies between the ranges of `a` and `b`.
- An `if` expression: the union of the ranges of its arms.
- A `let` binding of the handler or of a `func`: the range of its value.
- A call of a `func` with a body: the union of the ranges of its `return` expressions, with each parameter given the range of its argument. A `func` that calls itself, directly or through other functions, SHALL be unknown on both sides.
- A condition that compares a `let` binding, a parameter, or a field the handler does not write, with a constant by `<`, `<=`, `>` or `>=`: inside the arm where the condition holds the value SHALL have the narrowed range, inside the other arm the opposite one, and after an arm that always returns the rest of the block SHALL count as the other arm.

Every other expression SHALL be unknown on both sides. This includes a `var` field that is not `clamped`, an event field, a `float` `let` field, and a call of an `extern func` not listed above.

#### Scenario: A periodic phase gives a constant weight
- **WHEN** `fixed_tick` declares `every: 1s / 60` and the weight is `8.0 * fixed_tick.dt`
- **THEN** both bounds of the weight are the binary32 value of `8.0 * fixed_tick.dt`, about `0.1333`

#### Scenario: A variable step has no upper bound
- **WHEN** `tick` declares `dt: float = frame.dt` and the weight is `8.0 * tick.dt`
- **THEN** the lower bound is `0.0` and the upper bound is unknown

#### Scenario: A duration step through seconds
- **WHEN** `tick` declares `delta: duration = frame.delta` and the weight is `8.0 * seconds(tick.delta)`
- **THEN** the lower bound is `0.0` and the upper bound is unknown

#### Scenario: min gives the upper bound
- **WHEN** the weight is `math.min(1.0, tick.dt * TURN_RATE)` and `TURN_RATE` is a positive constant
- **THEN** the weight lies between `0.0` and `1.0`

#### Scenario: A var field is unknown
- **WHEN** the weight is `follow.smoothing * fixed_tick.dt` and `smoothing` is a `var` field with no `clamped`
- **THEN** both bounds are unknown

#### Scenario: A clamped field gives its bounds
- **WHEN** a trait declares `var grip: float clamped(min = 0.0, max = 30.0) = 10.0` and the weight is `u.grip * fixed_tick.dt` with `fixed_tick` declaring `every: 1s / 60`
- **THEN** the weight lies between `0.0` and the binary32 value of `30.0 * fixed_tick.dt`, about `0.5`

#### Scenario: A clamped field bounded by a float let field
- **WHEN** a trait declares `let top: float = 1.0` and `var k: float clamped(min = 0.0, max = top) = 0.0`, and the weight is `u.k`
- **THEN** the lower bound is `0.0` and the upper bound is unknown

#### Scenario: Progress as a ratio of durations
- **WHEN** a trait declares `var elapsed: duration clamped(min = 0s, max = 2s) = 0s` and the weight is `u.elapsed / 2s`
- **THEN** the weight lies between `0.0` and `1.0`

#### Scenario: A ratio over a duration that can be zero
- **WHEN** the weight is `1s / tick.delta`
- **THEN** both bounds are unknown

#### Scenario: A condition narrows a field
- **WHEN** a handler binds `let blend = if follow.smoothing <= 0.0: 1.0 else: math.min(1.0, follow.smoothing * step.dt)`, does not write `follow.smoothing`, and `step.dt` is the root frame's `dt`
- **THEN** `blend` lies between `0.0` and `1.0`

#### Scenario: A func with an early return
- **WHEN** `func follow_blend(smoothing: float, dt: float) float` returns `1.0` when `smoothing <= 0.0` and otherwise returns `math.min(1.0, smoothing * dt)`, and it is called with a `var` field and the root frame's `dt`
- **THEN** the call lies between `0.0` and `1.0`

#### Scenario: A recursive func is unknown
- **WHEN** the weight calls a `func` that calls itself
- **THEN** both bounds are unknown

### Requirement: A weight proven inside the unit interval is silent
The compiler SHALL report nothing for a blend whose derived lower bound is at least `0.0` and whose derived upper bound is at most `1.0`.

#### Scenario: Constant half
- **WHEN** a handler runs `me.x += (order.target[Unit].x - me.x) * 0.5`
- **THEN** no warning is reported

#### Scenario: The standard library and the examples are silent
- **WHEN** every module of `stdlib/` and every program of `examples/` is compiled
- **THEN** the check reports no warning

### Requirement: A weight proven outside the unit interval is warned
The compiler SHALL warn, naming the rule and the statement's source location, when the derived lower bound of a blend weight is above `1.0`, or its derived upper bound is below `0.0`. The warning SHALL say that the field always passes its goal in the first case and always moves away from its goal in the second, and SHALL give the derived bound. It SHALL be a warning and never an error.

#### Scenario: Constant above one
- **WHEN** a handler runs `u.speed += (wanted - u.speed) * 1.5`
- **THEN** the compiler warns that `u.speed` always passes its goal, names the rule, and gives the weight `1.5`

#### Scenario: A fixed step with a large rate
- **WHEN** `fixed_tick` declares `every: 1s / 60` and a handler runs `u.speed += (wanted - u.speed) * 90.0 * fixed_tick.dt`
- **THEN** the compiler warns that `u.speed` always passes its goal and gives the weight as the program computes it, about `1.5`

#### Scenario: Negative constant
- **WHEN** a handler runs `u.speed += (wanted - u.speed) * -0.25`
- **THEN** the compiler warns that `u.speed` always moves away from its goal

#### Scenario: Compilation still succeeds
- **WHEN** a program has a blend with the constant weight `1.5` and `--warnings-as-errors` is not set
- **THEN** compilation succeeds and the generated program is the same as without the check

### Requirement: A weight not proven inside the unit interval is warned with its cause
For every other blend, where a bound is unknown or the range reaches outside `[0, 1]` without lying wholly outside it, the compiler SHALL warn, naming the rule and the statement's source location. The warning SHALL say which side is not proven. For an unknown side it SHALL name one source expression that has no bound on that side; for a known side it SHALL give the bound. It SHALL name a fix for each side it reports: for the upper side, running the rule in a periodic phase when the unbounded source is a phase `dt` or `delta`, and `math.min(1.0, ...)`; for the lower side, `math.max(0.0, ...)`; and `math.clamp(..., 0.0, 1.0)` when both sides are reported. When the named source is a `var` field, the warning SHALL also name declaring that field `clamped` with a bound on each reported side. A blend SHALL get at most one warning.

#### Scenario: Rate times a variable step
- **WHEN** a `tick` handler runs `u.speed += (wanted - u.speed) * ACCEL * tick.dt` with a positive constant `ACCEL`
- **THEN** the compiler warns that the weight has no upper bound, names `tick.dt`, and names both fixes: a periodic phase and `math.min(1.0, ...)`

#### Scenario: A var field in a fixed step
- **WHEN** a `fixed_tick` handler runs `pose.position += (aim - pose.position) * follow.smoothing * fixed_tick.dt` and `smoothing` is a `var` field with no `clamped`
- **THEN** the compiler warns that neither side is proven, names `follow.smoothing`, and names `math.clamp(..., 0.0, 1.0)` and declaring `smoothing` with `clamped(min = ..., max = ...)`

#### Scenario: min without a lower bound
- **WHEN** a handler runs `u.speed += (wanted - u.speed) * math.min(1.0, u.rate * tick.dt)` and `rate` is a `var` field with no `clamped`
- **THEN** the compiler warns that the lower side is not proven, names `u.rate`, and names `math.max(0.0, ...)` and declaring `rate` with `clamped(min = ...)`

#### Scenario: A clamped field that is too wide
- **WHEN** a trait declares `var k: float clamped(min = 0.0, max = 1.5) = 0.0` and a handler runs `u.speed += (wanted - u.speed) * u.k`
- **THEN** the compiler warns that the weight can reach `1.5`

#### Scenario: A known bound that is too wide
- **WHEN** a handler runs `u.speed += (wanted - u.speed) * math.clamp(k, 0.0, 1.5)` for an unknown `k`
- **THEN** the compiler warns that the weight can reach `1.5`

#### Scenario: One warning per statement
- **WHEN** a blend's weight is unknown on both sides
- **THEN** the compiler reports one warning for that statement
