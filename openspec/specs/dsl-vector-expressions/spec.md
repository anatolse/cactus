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
type `float` holding the channel value with no bound (see "Color channels are unbounded floats"),
using the same member-chain resolution mechanism `vec2`/`vec3`'s `.x`/`.y`/`.z` component access
already uses. No other member name is valid on a `color`-typed expression.

#### Scenario: Reading a color channel

- **WHEN** source contains `shape.color.a` where `shape.color` is `color`
- **THEN** the expression has type `float`, representing the color's alpha channel

#### Scenario: Reading each channel

- **WHEN** source contains `c.r`, `c.g`, `c.b`, and `c.a` where `c` is `color`
- **THEN** each expression has type `float`, corresponding to the color's red, green, blue, and
  alpha channels respectively

#### Scenario: Reading a channel above one

- **WHEN** `c` is `#FF000080 * 3.0` and source reads `c.r` and `c.a`
- **THEN** `c.r` is 3.0 and `c.a` is the binary32 product of 128/255 and 3.0

#### Scenario: Invalid channel name rejected

- **WHEN** source contains `c.w` where `c` is `color`
- **THEN** the semantic analyzer reports a compile-time error naming `color` and the invalid
  member `w`

### Requirement: Precise diagnostics for unsupported color expressions

A compile-time error for an unsupported `color` operator combination, constructor call, or
invalid member access SHALL name the operand type(s), the operator or member name involved, and
(for the constructor) the provided argument count.

#### Scenario: Binary operator diagnostic names operand types

- **WHEN** source contains `a - b` where `a` is `color` and `b` is `float`
- **THEN** the reported error names `color`, `float`, and the `-` operator

### Requirement: Color channels are unbounded floats
A `color` SHALL hold four `float` channels, red, green, blue and alpha, that follow `dsl-numeric-semantics`. No color operation SHALL clamp or round a channel, so a channel MAY be below 0 or above 1. A hex literal `#RRGGBBAA` SHALL give each channel its byte value divided by 255. Channel values SHALL be sRGB codes, and alpha SHALL be straight (not premultiplied).

#### Scenario: A blend toward a darker color
- **WHEN** a handler computes `a + (b - a) * 0.5` with `a = #FFFFFFFF` and `b = #000000FF`
- **THEN** each of the red, green and blue channels is 0.5

#### Scenario: A fade reaches zero
- **WHEN** a handler multiplies the alpha of `#FFFFFFFF` by 0.99 once per frame for 1000 frames
- **THEN** the alpha is the binary32 result of those 1000 multiplications, below 0.0001

#### Scenario: Brighter than white
- **WHEN** a handler computes `#FFFFFFFF * 2.0`
- **THEN** every channel of the result is 2.0

### Requirement: Colors are clamped only at the output
A color SHALL be clamped to 0..1 and rounded to 8 bits only where it enters a consumer that holds 8 bits per channel (`backend-cpp-entt`, "Colors convert to 8 bits at 8-bit consumers"). A rule handler and a render-stage handler SHALL give the same channel values for the same color expression.

#### Scenario: Rule and render stage agree
- **WHEN** the same expression `(a - b) + b` over colors is evaluated in a rule handler and in a fragment-stage handler
- **THEN** both results have the channels of `a`

#### Scenario: Stored value is not clamped by drawing
- **WHEN** a sprite's color is `#FFFFFFFF * 2.0` and the sprite is drawn
- **THEN** a later read of the color still gives 2.0 in every channel

### Requirement: Color constructor keeps channel values
`color(r: float, g: float, b: float, a: float)` SHALL construct a `color` from four channel values, stored exactly with no clamp. An `int` argument SHALL be accepted and promoted to `float`, identically to how `vec2(...)`/`vec3(...)` promote mixed `int`/`float` arguments. Any other argument count, or an argument whose type is neither `float`- nor `int`-compatible, SHALL be a compile-time error.

#### Scenario: Constructing a color from channel values
- **WHEN** source contains `color(1.0, 0.0, 0.0, 1.0)`
- **THEN** the expression has type `color`, equivalent to the hex literal `#FF0000FF`

#### Scenario: Constructor keeps out-of-range channels
- **WHEN** source contains `color(1.5, -0.2, 0.0, 1.0)`
- **THEN** the expression has type `color` whose red channel is 1.5 and green channel is -0.2

#### Scenario: Wrong constructor arity rejected
- **WHEN** source contains `color(1.0, 0.0, 0.0)`
- **THEN** the semantic analyzer reports a compile-time error naming `color` and the provided argument count

#### Scenario: Int argument promoted to float
- **WHEN** source contains `color(k, 0, 0, 1)` where `k` is an `int`
- **THEN** the expression is accepted, has type `color`, and each `int` argument is converted to `float` before construction

### Requirement: Color binary operator matrix without clamp
The semantic analyzer SHALL accept exactly these `color` operator rows, each computing every channel as one `float` operation with no clamp: `color + color`, `color - color` and `color * color` (component-wise), `color * float`, `float * color` and `color / float` (each channel scaled). No other combination with a `color` operand SHALL be valid, including `color` with `+`/`-` against a `float` and division by a `color`.

#### Scenario: Component-wise color addition
- **WHEN** source contains `a + b` where `a` and `b` are `color`
- **THEN** the expression has type `color`, each channel the sum of the corresponding input channels

#### Scenario: Scalar multiplication scales every channel
- **WHEN** source contains `tint * 0.5` where `tint` is `color`
- **THEN** the expression has type `color` with each of its four channels scaled by `0.5`

#### Scenario: Chained arithmetic keeps intermediate values
- **WHEN** source contains `(tint * 2.0) * 0.5` where `tint` is `#FFFFFFFF`
- **THEN** `tint * 2.0` has every channel 2.0 and the final result is `#FFFFFFFF`

#### Scenario: Color plus scalar rejected
- **WHEN** source contains `tint + 1.0` where `tint` is `color`
- **THEN** the semantic analyzer reports a compile-time error naming `color`, `float`, and the `+` operator

#### Scenario: Color-by-color division rejected
- **WHEN** source contains `a / b` where both operands are `color`
- **THEN** the semantic analyzer reports a compile-time error; only a `float` divisor is accepted
