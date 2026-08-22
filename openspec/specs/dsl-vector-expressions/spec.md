# dsl-vector-expressions Specification

## Purpose

Lets gameplay code written in vec2/vec3-heavy areas use natural constructor and
operator syntax instead of manually decomposing every vector expression into
component-by-component (`.x`/`.y`/`.z`) arithmetic.

## Requirements

### Requirement: Scalar splat constructors for vec2 and vec3
`vec2(float)` SHALL construct a `vec2` with both components set to the given scalar.
`vec3(float)` SHALL construct a `vec3` with all three components set to the given
scalar. The existing two-argument `vec2(float, float)` and three-argument
`vec3(float, float, float)` component constructors remain valid and unchanged. An
`int` argument SHALL be accepted and promoted to `float`, identically to how mixed
`int`/`float` binary arithmetic already promotes. Any other argument count for
`vec2(...)`/`vec3(...)`, or an argument whose type is neither `float`- nor
`int`-compatible, SHALL be a compile-time error.

#### Scenario: vec2 scalar splat
- **WHEN** source contains `vec2(0.0)`
- **THEN** the expression has type `vec2` and is equivalent to `vec2(0.0, 0.0)`

#### Scenario: vec3 scalar splat
- **WHEN** source contains `vec3(scale)` where `scale` is a `float`
- **THEN** the expression has type `vec3` and is equivalent to `vec3(scale, scale, scale)`

#### Scenario: Existing two-argument constructor remains valid
- **WHEN** source contains `vec2(400.0, 40.0)`
- **THEN** the expression has type `vec2` with `x = 400.0`, `y = 40.0`

#### Scenario: Wrong constructor arity rejected
- **WHEN** source contains `vec2(1.0, 2.0, 3.0)`
- **THEN** the semantic analyzer reports a compile-time error naming `vec2` and the
  provided argument count

#### Scenario: Int argument promoted to float
- **WHEN** source contains `vec2(k, 0)` where `k` is an `int`
- **THEN** the expression is accepted, has type `vec2`, and the generated code
  converts each `int` argument to `float` before construction

#### Scenario: Non-float constructor argument rejected
- **WHEN** source contains `vec2("0", "0")`
- **THEN** the semantic analyzer reports a compile-time error naming the argument's
  actual type

### Requirement: Fixed vec2/vec3 binary operator matrix
The semantic analyzer SHALL accept exactly the following `(left type, operator, right
type) -> result type` combinations for `vec2`/`vec3` operands, and no others:

| Left | Operator | Right | Result |
| --- | --- | --- | --- |
| `vec2` | `+` | `vec2` | `vec2` |
| `vec3` | `+` | `vec3` | `vec3` |
| `vec2` | `-` | `vec2` | `vec2` |
| `vec3` | `-` | `vec3` | `vec3` |
| `vec2` | `*` | `float` | `vec2` |
| `float` | `*` | `vec2` | `vec2` |
| `vec3` | `*` | `float` | `vec3` |
| `float` | `*` | `vec3` | `vec3` |
| `vec2` | `/` | `float` | `vec2` |
| `vec3` | `/` | `float` | `vec3` |
| `vec2` | `*` | `vec2` | `vec2` (component-wise) |
| `vec3` | `*` | `vec3` | `vec3` (component-wise) |

`vec2 * vec2` and `vec3 * vec3` SHALL perform component-wise multiplication, never a
dot product. No other operator/operand-type combination involving a `vec2` or `vec3`
operand is valid; this includes mismatched-dimension operands, vector-by-vector
division, and a vector operand combined with a bare `+`/`-` against a `float`.

#### Scenario: Component addition
- **WHEN** source contains `a.position + b.position` where both operands are `vec2`
- **THEN** the expression has type `vec2` and evaluates to component-wise addition

#### Scenario: Component-wise multiply is not a dot product
- **WHEN** source contains `a * b` where both operands are `vec2`
- **THEN** the expression has type `vec2` with each component the product of the
  corresponding input components, not the scalar dot product of `a` and `b`

#### Scenario: Scalar multiplication is commutative in accepted syntax
- **WHEN** source contains both `velocity * dt` and `dt * velocity`, where `velocity`
  is `vec2` and `dt` is `float`
- **THEN** both expressions are accepted and have type `vec2`

#### Scenario: Mismatched vector dimensions rejected
- **WHEN** source contains `a + b` where `a` is `vec2` and `b` is `vec3`
- **THEN** the semantic analyzer reports a compile-time error naming both operand
  types and the operator

#### Scenario: Vector-by-vector division rejected
- **WHEN** source contains `a / b` where both operands are `vec2`
- **THEN** the semantic analyzer reports a compile-time error; only a `float` divisor
  is accepted

#### Scenario: Vector plus scalar rejected
- **WHEN** source contains `position + 1.0` where `position` is `vec2`
- **THEN** the semantic analyzer reports a compile-time error; `+`/`-` require both
  operands to be the same vector type

### Requirement: Compound assignment on vec2/vec3-typed writable targets
`+=`, `-=`, `*=`, and `/=` SHALL be accepted on a writable assignment target (a
handler-local `var`, a writable selected trait field, or a writable vector component
reached through a selected trait alias) whenever the corresponding row of the fixed
binary-operator matrix has the target's type as both its left operand type and its
result type. Any other combination SHALL be a compile-time error. Compound assignment
through a pair-bound trait alias remains rejected, matching the existing read-only
rule for pair-bound trait access.

#### Scenario: Vector addition compound assignment
- **WHEN** a handler contains `transform.position += motion.velocity * tick.dt` where
  `position` and `velocity` are `vec2` fields
- **THEN** the assignment is accepted and updates `position` by component-wise addition

#### Scenario: Vector scalar-multiply compound assignment
- **WHEN** a handler contains `transform.scale *= growth` where `scale` is `vec3` and
  `growth` is `float`
- **THEN** the assignment is accepted and scales every component of `scale`

#### Scenario: Incompatible compound-assignment operand rejected
- **WHEN** a handler contains `transform.position += 5` where `position` is `vec2`
- **THEN** the semantic analyzer reports a compile-time error; `float` is not a valid
  right-hand operand for `vec2 +=`

#### Scenario: Pair-bound compound assignment remains rejected
- **WHEN** a pair handler contains `body.velocity *= 2.0` where `body` is a pair-bound
  trait alias
- **THEN** the semantic analyzer reports the existing pair-bound read-only error

### Requirement: Precise diagnostics for unsupported vector expressions
A compile-time error for an unsupported vector operator combination or unsupported
compound assignment SHALL name the operand type(s) and operator involved. A
compile-time error for an unsupported compound assignment SHALL additionally name the
assignment target's type and the source expression's type.

#### Scenario: Binary operator diagnostic names operand types
- **WHEN** source contains `a * b` where `a` is `vec2` and `b` is `vec3`
- **THEN** the reported error names `vec2`, `vec3`, and the `*` operator

#### Scenario: Compound assignment diagnostic names target and source types
- **WHEN** a handler contains `transform.position -= "east"` where `position` is
  `vec2`
- **THEN** the reported error names the target type `vec2`, the operator `-=`, and the
  source expression's type `string`

### Requirement: Color float component access

A `color`-typed expression SHALL support member access `.r`, `.g`, `.b`, `.a`, each resolving to
type `float` normalized to the range `[0.0, 1.0]`, using the same member-chain resolution
mechanism `vec2`/`vec3`'s `.x`/`.y`/`.z` component access already uses. No other member name is
valid on a `color`-typed expression.

#### Scenario: Reading a color channel

- **WHEN** source contains `shape.color.a` where `shape.color` is `color`
- **THEN** the expression has type `float` in `[0.0, 1.0]`, representing the color's alpha channel

#### Scenario: Reading each channel

- **WHEN** source contains `c.r`, `c.g`, `c.b`, and `c.a` where `c` is `color`
- **THEN** each expression has type `float`, corresponding to the color's red, green, blue, and
  alpha channels respectively

#### Scenario: Invalid channel name rejected

- **WHEN** source contains `c.w` where `c` is `color`
- **THEN** the semantic analyzer reports a compile-time error naming `color` and the invalid
  member `w`

### Requirement: Color constructor

`color(r: float, g: float, b: float, a: float)` SHALL construct a `color` from four channel
values, each independently clamped to `[0.0, 1.0]` before being stored. An `int` argument SHALL be
accepted and promoted to `float`, identically to how `vec2(...)`/`vec3(...)` already promote mixed
`int`/`float` arguments. Any other argument count, or an argument whose type is neither `float`-
nor `int`-compatible, SHALL be a compile-time error.

#### Scenario: Constructing a color from channel values

- **WHEN** source contains `color(1.0, 0.0, 0.0, 1.0)`
- **THEN** the expression has type `color`, equivalent to the hex literal `#FF0000FF`

#### Scenario: Constructor clamps out-of-range channels

- **WHEN** source contains `color(1.5, -0.2, 0.0, 1.0)`
- **THEN** the expression has type `color` with red and green clamped to `1.0` and `0.0`
  respectively before storage

#### Scenario: Wrong constructor arity rejected

- **WHEN** source contains `color(1.0, 0.0, 0.0)`
- **THEN** the semantic analyzer reports a compile-time error naming `color` and the provided
  argument count

#### Scenario: Int argument promoted to float

- **WHEN** source contains `color(k, 0, 0, 1)` where `k` is an `int`
- **THEN** the expression is accepted, has type `color`, and each `int` argument is converted to
  `float` before construction

### Requirement: Fixed color binary operator matrix

The semantic analyzer SHALL accept exactly the following `(left type, operator, right type) ->
result type` combinations for `color` operands, and no others — the same shape as the existing
`vec2`/`vec3` operator matrix (`dsl-vector-expressions`, "Fixed vec2/vec3 binary operator
matrix"):

| Left | Operator | Right | Result |
| --- | --- | --- | --- |
| `color` | `+` | `color` | `color` (component-wise, each channel clamped) |
| `color` | `-` | `color` | `color` (component-wise, each channel clamped) |
| `color` | `*` | `float` | `color` (each channel scaled, then clamped) |
| `float` | `*` | `color` | `color` (each channel scaled, then clamped) |
| `color` | `/` | `float` | `color` (each channel scaled, then clamped) |
| `color` | `*` | `color` | `color` (component-wise, each channel clamped) |

No other operator/operand-type combination involving a `color` operand is valid, including a
`color` operand combined with a bare `+`/`-` against a `float`, and division by a `color`.

#### Scenario: Component-wise color addition

- **WHEN** source contains `a + b` where `a` and `b` are `color`
- **THEN** the expression has type `color`, each channel the clamped sum of the corresponding
  input channels

#### Scenario: Scalar multiplication scales every channel except is clamped

- **WHEN** source contains `tint * 0.5` where `tint` is `color`
- **THEN** the expression has type `color` with each of its four channels scaled by `0.5` and
  clamped to `[0.0, 1.0]`

#### Scenario: Color plus scalar rejected

- **WHEN** source contains `tint + 1.0` where `tint` is `color`
- **THEN** the semantic analyzer reports a compile-time error naming `color`, `float`, and the `+`
  operator

#### Scenario: Color-by-color division rejected

- **WHEN** source contains `a / b` where both operands are `color`
- **THEN** the semantic analyzer reports a compile-time error; only a `float` divisor is accepted

### Requirement: Color arithmetic clamps at every step, not only at final output

Every `color`-producing expression (the constructor and every operator-matrix row) SHALL clamp
each channel to `[0.0, 1.0]` before the result is stored, including intermediate results of a
chained expression. `color` values SHALL NOT retain magnitude beyond `[0.0, 1.0]` per channel at
any point — this is a deliberate low-dynamic-range design, not a rounding artifact, and a
multi-step arithmetic expression's result reflects clamping at each intermediate step rather than
on the final value alone.

#### Scenario: Chained arithmetic clips at each step

- **WHEN** source contains `(tint * 2.0) * 0.5` where `tint` is an opaque, saturated `color` (e.g.
  `#FFFFFFFF`)
- **THEN** `tint * 2.0` clamps to `#FFFFFFFF` (already at maximum), and the final result is
  `#FFFFFFFF` scaled by `0.5`, not `tint` scaled by `1.0` — chained arithmetic is not equivalent to
  evaluating the combined scale factor against unclamped intermediate values

### Requirement: Precise diagnostics for unsupported color expressions

A compile-time error for an unsupported `color` operator combination, constructor call, or
invalid member access SHALL name the operand type(s), the operator or member name involved, and
(for the constructor) the provided argument count.

#### Scenario: Binary operator diagnostic names operand types

- **WHEN** source contains `a - b` where `a` is `color` and `b` is `float`
- **THEN** the reported error names `color`, `float`, and the `-` operator
