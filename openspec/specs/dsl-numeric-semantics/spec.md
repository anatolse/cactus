# dsl-numeric-semantics Specification

## Purpose
Define the one meaning of `int` and `float` arithmetic, comparison, division and conversion that every backend follows, so that every numeric expression has a defined result and the same program computes the same bits on every platform.

## Requirements

### Requirement: Integers wrap modulo 2³²
`int` SHALL be a 32-bit two's-complement integer. The operators `+`, `-`, `*`, unary `-`, and the compound assignments `+=`, `-=` and `*=` on `int` SHALL compute the exact mathematical result and wrap it modulo 2³² into the `int` range. No integer operation SHALL have undefined behavior or stop the program.

#### Scenario: Addition wraps at the upper bound
- **WHEN** a handler evaluates `2147483647 + 1` on `int` values
- **THEN** the result is `-2147483648`

#### Scenario: Multiplication wraps
- **WHEN** a handler evaluates `65536 * 65536` on `int` values
- **THEN** the result is `0`

#### Scenario: Negating the minimum value
- **WHEN** a handler evaluates unary `-` on an `int` holding `-2147483648`
- **THEN** the result is `-2147483648`

#### Scenario: Compound assignment wraps
- **WHEN** a trait field `n: int` holds `2147483647` and a handler runs `n += 2`
- **THEN** the field holds `-2147483647`

### Requirement: Integer division is floor division and total
For `int` operands, `a / b` SHALL be the floor of the exact quotient, and `a % b` SHALL be `a - (a / b) * b`, so for `b > 0` the remainder is in `[0, b)` and for `b < 0` it is in `(b, 0]`. Division by zero SHALL be defined: `a / 0` is `0` and `a % 0` is `a`, so `a == (a / b) * b + a % b` holds for every `a` and `b`. `-2147483648 / -1` SHALL wrap to `-2147483648`, and `-2147483648 % -1` SHALL be `0`. `/=` and `%=` SHALL follow the same rules.

#### Scenario: Negative dividend rounds down
- **WHEN** a handler evaluates `-7 / 2` and `-7 % 2`
- **THEN** the results are `-4` and `1`

#### Scenario: Negative divisor
- **WHEN** a handler evaluates `7 / -2` and `7 % -2`
- **THEN** the results are `-4` and `-1`

#### Scenario: Grid index of a negative coordinate
- **WHEN** a handler evaluates `-1 % 8`
- **THEN** the result is `7`

#### Scenario: Division by zero
- **WHEN** a handler evaluates `5 / 0` and `5 % 0`
- **THEN** the results are `0` and `5`, and the program keeps running

#### Scenario: Minimum value divided by minus one
- **WHEN** a handler evaluates `-2147483648 / -1`
- **THEN** the result is `-2147483648`

### Requirement: Floats are IEEE-754 binary32
`float` SHALL be an IEEE-754 binary32 value. Every float operation SHALL round its exact result to nearest, ties to even, exactly once per operation, as written in the source. Vector and color components SHALL be `float` values that follow the same rules. When an operator mixes `int` and `float` operands, the `int` operand SHALL first be converted to the nearest `float`, ties to even. Float division by zero, overflow and invalid operations SHALL produce the IEEE-754 results (`±inf` or NaN).

#### Scenario: Mixed operands convert the integer
- **WHEN** a handler evaluates `16777217 + 0.0` with an `int` left operand
- **THEN** the result is the `float` `16777216.0`

#### Scenario: Float division by zero
- **WHEN** a handler evaluates `1.0 / 0.0`
- **THEN** the result is positive infinity

### Requirement: Float results are bit-identical across platforms
For code that runs on the CPU, the same program with the same inputs SHALL produce bit-identical `float` results on every supported platform and compiler. A backend SHALL NOT fuse a multiply and an add into one rounding, SHALL NOT reassociate or otherwise reorder float operations, and SHALL NOT use approximate math modes. Library functions that the language or stdlib defines SHALL also return bit-identical results on every platform. Render-pass stage handlers run on the GPU; their `float` results follow GLSL precision and are excluded from this requirement.

#### Scenario: Multiply-add is rounded twice
- **WHEN** a handler evaluates `a * b + c` on `float` values
- **THEN** the result equals rounding `a * b` to binary32, then rounding the sum with `c` to binary32, on every platform

#### Scenario: Same replay on two platforms
- **WHEN** the same program runs the same recorded inputs on Windows and on Linux
- **THEN** every `float` trait field holds the same bits after each fixed step on both platforms

#### Scenario: Stage handlers are excluded
- **WHEN** a fragment-stage handler computes a color from `float` arithmetic
- **THEN** the result follows GLSL precision and may differ between GPUs

### Requirement: Float comparisons follow IEEE-754
The comparison operators `==`, `!=`, `<`, `<=`, `>` and `>=` on `float` SHALL follow IEEE-754: any comparison with NaN is false except `!=`, which is true, and `-0.0 == 0.0` is true. Sort keys use the separate total order defined by `dsl-rule-order-by`.

#### Scenario: NaN is not equal to itself
- **WHEN** `x` is NaN and a handler evaluates `x == x`
- **THEN** the result is `false`

#### Scenario: Signed zeros are equal
- **WHEN** a handler evaluates `-0.0 == 0.0`
- **THEN** the result is `true`

### Requirement: Float-to-integer conversion is total
Every conversion from `float` to `int` SHALL be defined for every input. It SHALL first apply its rounding rule (floor, ceiling or nearest), then map NaN to `0`, values above `2147483647` (including `+inf`) to `2147483647`, and values below `-2147483648` (including `-inf`) to `-2147483648`.

#### Scenario: NaN converts to zero
- **WHEN** a float-to-int conversion receives NaN
- **THEN** the result is `0`

#### Scenario: Large values clamp
- **WHEN** a float-to-int conversion receives `1.0e20` or `-inf`
- **THEN** the results are `2147483647` and `-2147483648`

### Requirement: Integer semantics hold in render-pass stage handlers
Integer `/` and `%` in a render-pass stage handler SHALL follow the floor-division and division-by-zero rules of this capability, even though GLSL integer division differs.

#### Scenario: Stage handler divides by zero
- **WHEN** a vertex-stage handler evaluates `i / 0` and `i % 0` on `int` values
- **THEN** the results are `0` and `i`

#### Scenario: Stage handler floor division
- **WHEN** a fragment-stage handler evaluates `-7 / 2`
- **THEN** the result is `-4`

### Requirement: Float accumulation into one field is exact
Within one activation, take the Accum writes (`+=`, `-=`) to one `float` field of one entity that run between two consecutive non-Accum accesses of that field, or between such an access and the activation's start or commit. They may come from several handlers, several rows of one handler, several event occurrences, or several statements of one handler execution. Each such write contributes a delta: `e` for `+=` and `-e` for `-=`. The field's value after them SHALL be its value before them plus all their deltas, summed exactly and rounded once to binary32, with the edge rules of a float `sum` reduction (NaN, infinities, overflow and the sign of an exact zero). The result SHALL NOT depend on the order of the writes. `vec2` and `vec3` fields, and Accum writes to one of their components, SHALL follow this rule per component. `int` Accum writes wrap and need no merge, because wrapping addition does not depend on order.

#### Scenario: Two handlers add to one float in either order
- **WHEN** a named entity's `Tally.total` is `1.0e8`, and in one activation handler A runs `Game[Tally].total += 1.0` and handler B runs `Game[Tally].total -= 1.0e8`, with no order between them
- **THEN** after the activation `Tally.total` is `1.0`, and swapping A and B's declaration order gives the same value

#### Scenario: Several rows add to one float
- **WHEN** ten entities each run `Game[Tally].total += 0.1` in one handler and `Tally.total` starts at `0.0`
- **THEN** `Tally.total` is the binary32 value nearest to the exact sum of ten binary32 `0.1` values

#### Scenario: A reader sees the merged value
- **WHEN** handlers A and B Accum-write `Pool.level` and handler C reads `Pool.level` after both
- **THEN** C reads the exactly merged value

#### Scenario: A lone accumulation is unchanged
- **WHEN** only one handler Accum-writes `Body.x` once per row on its own entity
- **THEN** the result equals `round(x + e)`, as for a plain `+=`

### Requirement: Float min and max are commutative
`math.min` and `math.max` on `float` SHALL be commutative, associative and idempotent: NaN in either argument SHALL give NaN, `math.max(-0.0, 0.0)` and `math.max(0.0, -0.0)` SHALL both be `0.0`, and `math.min(-0.0, 0.0)` and `math.min(0.0, -0.0)` SHALL both be `-0.0`.

#### Scenario: Signed zeros order the same both ways
- **WHEN** a handler computes `math.max(-0.0, 0.0)` and `math.max(0.0, -0.0)`
- **THEN** both results are `0.0` with a positive sign bit
