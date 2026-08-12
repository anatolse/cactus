## Requirements

### Requirement: std.math provides scalar math pure functions
The `std.math` module SHALL provide commonly needed scalar math functions as pure funcs. All functions SHALL have no side effects, SHALL be callable from both pure `func` declarations and rule handlers, and SHALL be backed by concrete runtime/backend-library implementations rather than declaration-only symbols.

Scalar helpers that do not require runtime state SHALL use allocation-free implementations and SHALL be declared `constexpr` and `noexcept` where the C++ operation semantics permit it.

#### Scenario: Interpolation function
- **WHEN** `use std.math as math` is imported and `math.lerp(0.0, 10.0, 0.5)` is called
- **THEN** it returns `5.0` (linear interpolation between a and b by t)

#### Scenario: Clamp function
- **WHEN** `math.clamp(15.0, 0.0, 10.0)` is called
- **THEN** it returns `10.0` (value clamped to [lo, hi] range)

#### Scenario: PI constant
- **WHEN** `math.PI` is referenced after `use std.math as math`
- **THEN** it evaluates to approximately `3.14159265`

#### Scenario: Floor/ceil return int
- **WHEN** `math.floor(3.7)` is called
- **THEN** it returns integer `3`

#### Scenario: Scalar extern coverage is behaviorally verified
- **WHEN** the backend/runtime test suite runs
- **THEN** it includes unit tests covering declared scalar extern functions including `lerp`, `clamp`, `abs`, `min`, `max`, `sqrt`, `sin`, `cos`, `atan2`, `floor`, `ceil`, `round`, and `pow`

---

### Requirement: std.math.vec2 provides 2D vector pure functions
The `std.math.vec2` module SHALL provide pure functions operating on `vec2` values. All functions SHALL accept `vec2` arguments and return `vec2` or `float` as appropriate. Declared extern functions SHALL be backed by concrete runtime/backend-library implementations and verified by behavioral tests.

#### Scenario: Vector length
- **WHEN** `use std.math.vec2 as v2` is imported and `v2.length(vec2(3.0, 4.0))` is called
- **THEN** it returns `5.0` (Euclidean length)

#### Scenario: Vector normalization
- **WHEN** `v2.normalize(vec2(3.0, 4.0))` is called
- **THEN** it returns a vec2 with length 1.0 in the same direction

#### Scenario: Dot product
- **WHEN** `v2.dot(vec2(1.0, 0.0), vec2(0.0, 1.0))` is called
- **THEN** it returns `0.0` (perpendicular vectors)

#### Scenario: Lerp between vectors
- **WHEN** `v2.lerp(vec2(0.0, 0.0), vec2(10.0, 10.0), 0.5)` is called
- **THEN** it returns `vec2(5.0, 5.0)`

#### Scenario: Vec2 extern coverage is behaviorally verified
- **WHEN** the backend/runtime test suite runs
- **THEN** it includes unit tests covering declared vec2 extern functions including `length`, `normalize`, `dot`, `lerp`, `distance`, and `angle`

---

### Requirement: std.math.vec3 provides 3D vector pure functions
The `std.math.vec3` module SHALL provide pure functions operating on `vec3` values, including cross product (not available in 2D). Declared extern functions SHALL be backed by concrete runtime/backend-library implementations and verified by behavioral tests.

#### Scenario: 3D vector length
- **WHEN** `use std.math.vec3 as v3` is imported and `v3.length(vec3(1.0, 0.0, 0.0))` is called
- **THEN** it returns `1.0`

#### Scenario: Cross product
- **WHEN** `v3.cross(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0))` is called
- **THEN** it returns `vec3(0.0, 0.0, 1.0)` (right-hand rule)

#### Scenario: Reflection
- **WHEN** `v3.reflect(vec3(1.0, -1.0, 0.0), vec3(0.0, 1.0, 0.0))` is called
- **THEN** it returns `vec3(1.0, 1.0, 0.0)` (reflected off Y-normal surface)

#### Scenario: Vec3 extern coverage is behaviorally verified
- **WHEN** the backend/runtime test suite runs
- **THEN** it includes unit tests covering declared vec3 extern functions including `length`, `normalize`, `dot`, `cross`, `lerp`, `distance`, and `reflect`

---

### Requirement: std.math.quat provides quaternion pure functions
The `std.math.quat` module SHALL provide pure functions for 3D rotation using quaternions. It SHALL include construction, named-reference-frame composition, normalization, orientation comparison, direction extraction, and a raw dot product. Declared extern functions SHALL be backed by concrete runtime/backend-library implementations and verified by behavioral tests. Every function SHALL have a total (defined-for-every-input) result: zero-length or non-unit inputs SHALL produce the documented result rather than `NaN` or an unnormalized pass-through.

#### Scenario: Identity quaternion
- **WHEN** `use std.math.quat as quat` is imported and `quat.identity()` is called
- **THEN** it returns `quat(0.0, 0.0, 0.0, 1.0)` (no rotation)

#### Scenario: Forward vector from quaternion
- **WHEN** `quat.forward(quat.identity())` is called
- **THEN** it returns `vec3(0.0, 0.0, -1.0)` (default forward direction)

#### Scenario: Direction extraction normalizes its input
- **WHEN** `quat.forward`, `quat.right`, or `quat.up` is called with a non-unit quaternion
- **THEN** the quaternion is normalized before extracting the direction, and the result is a unit-length vector consistent with the normalized rotation

#### Scenario: Quaternion from Euler angles
- **WHEN** `quat.from_euler(0.0, math.PI, 0.0)` is called (180° yaw)
- **THEN** it returns a quaternion representing a 180° rotation around the Y axis, applied as pitch (around `+X`) first, then yaw (around `+Y`), then roll (around `+Z`)

#### Scenario: Axis-angle construction accepts a non-unit axis
- **WHEN** `quat.from_axis_angle(axis, angle)` is called with an `axis` that is not unit length
- **THEN** the axis is normalized before construction and the result is a unit quaternion

#### Scenario: Axis-angle construction handles a zero-length axis
- **WHEN** `quat.from_axis_angle(vec3(0.0, 0.0, 0.0), angle)` is called
- **THEN** it returns `quat.identity()`

#### Scenario: Spherical interpolation
- **WHEN** `quat.slerp(quat.identity(), some_rotation, 0.5)` is called
- **THEN** it returns a normalized quaternion halfway between identity and the target rotation

#### Scenario: Spherical interpolation clamps t and normalizes inputs
- **WHEN** `quat.slerp(from, to, t)` is called with `t` outside `[0.0, 1.0]`, or with `from`/`to` that are not unit length
- **THEN** `t` is clamped to `[0.0, 1.0]`, both inputs are normalized before interpolating, the shortest rotational path is taken (negating one input when their dot product is negative), and the result is a unit quaternion

#### Scenario: Spherical interpolation of two zero quaternions
- **WHEN** `quat.slerp(quat(0.0, 0.0, 0.0, 0.0), quat(0.0, 0.0, 0.0, 0.0), t)` is called
- **THEN** it returns `quat.identity()`

#### Scenario: Vector rotation by quaternion
- **WHEN** `quat.rotate(quat.identity(), vec3(1.0, 0.0, 0.0))` is called
- **THEN** it returns `vec3(1.0, 0.0, 0.0)` unchanged (identity rotation)

#### Scenario: Vector rotation normalizes its quaternion input
- **WHEN** `quat.rotate(rotation, value)` is called with a non-unit `rotation`
- **THEN** `rotation` is normalized before rotating `value`, so the length of `value` is preserved in the result

#### Scenario: Inverse of a non-zero quaternion
- **WHEN** `quat.inverse(q)` is called with a non-zero `q`
- **THEN** it returns the mathematical inverse of `q`

#### Scenario: Inverse of the zero quaternion
- **WHEN** `quat.inverse(quat(0.0, 0.0, 0.0, 0.0))` is called
- **THEN** it returns `quat.identity()`

#### Scenario: Explicit normalization
- **WHEN** `quat.normalize(value)` is called with a non-unit, non-zero `value`
- **THEN** it returns a unit quaternion representing the same rotation

#### Scenario: Normalizing the zero quaternion
- **WHEN** `quat.normalize(quat(0.0, 0.0, 0.0, 0.0))` is called
- **THEN** it returns `quat.identity()`

#### Scenario: Compose applies inner first, outer second
- **WHEN** `quat.compose(outer, inner)` is called
- **THEN** it returns a normalized quaternion equivalent to applying `inner`'s rotation first and `outer`'s rotation second, such that `quat.rotate(quat.compose(outer, inner), v)` equals `quat.rotate(outer, quat.rotate(inner, v))`

#### Scenario: Rotate around local axes
- **WHEN** `quat.rotate_local(current, delta)` is called
- **THEN** it returns `quat.compose(current, delta)` — a normalized quaternion representing `delta` applied around the entity's current local axes

#### Scenario: Rotate around world axes
- **WHEN** `quat.rotate_world(current, delta)` is called
- **THEN** it returns `quat.compose(delta, current)` — a normalized quaternion representing `delta` applied around world-space axes

#### Scenario: Local and world incremental rotation are observably different
- **WHEN** `quat.rotate_local(current, delta)` and `quat.rotate_world(current, delta)` are both called with the same non-identity `current` and the same non-identity `delta`
- **THEN** the two results are different quaternions

#### Scenario: Raw quaternion dot product
- **WHEN** `quat.dot(a, b)` is called
- **THEN** it returns the dot product of the raw (unnormalized) components of `a` and `b`, without normalizing either input

#### Scenario: Same-rotation comparison treats q and -q as equal
- **WHEN** `quat.same_rotation(q, negated_q, tolerance)` is called where `negated_q` is `q` with every component negated and `tolerance` is a valid non-negative value
- **THEN** it returns `true`

#### Scenario: Same-rotation comparison rejects a negative tolerance
- **WHEN** `quat.same_rotation(a, b, tolerance)` is called with a negative `tolerance`
- **THEN** it returns `false`

#### Scenario: Quaternion extern coverage is behaviorally verified
- **WHEN** the backend/runtime test suite runs
- **THEN** it includes unit tests covering declared quaternion extern functions including `identity`, `from_euler`, `from_axis_angle`, `forward`, `right`, `up`, `rotate`, `slerp`, `multiply`, `inverse`, `compose`, `rotate_local`, `rotate_world`, `normalize`, `same_rotation`, and `dot`

### Requirement: std.math.quat retains a raw compatibility multiply
The `std.math.quat` module SHALL provide `quat.multiply(a, b)`, computing the raw Hamilton product of `a` and `b` without normalizing the result, for source compatibility with existing callers. This function SHALL NOT be treated as the canonical composition call in new stdlib code, examples, or documentation.

#### Scenario: Multiply does not normalize
- **WHEN** `quat.multiply(a, b)` is called with non-unit `a` and/or `b`
- **THEN** it returns the raw Hamilton product without normalizing the result

#### Scenario: Existing multiply-based source keeps compiling
- **WHEN** existing authored code calls `quat.multiply(a, b)`
- **THEN** it compiles unchanged and produces the same raw Hamilton product as before this change

---

### Requirement: Math functions are pure and importable independently
Each math sub-module SHALL be independently importable without importing any other stdlib module. Math functions SHALL NOT depend on traits, events, or any game-specific types.

#### Scenario: Independent import
- **WHEN** only `use std.math.vec3 as v3` is imported (no other stdlib)
- **THEN** all `v3.*` functions are available and usable in pure func declarations and rule handlers

### Requirement: std.input query extern functions are backend-backed and verified
The `std.input` module SHALL provide concrete backend/runtime implementations for declared input query extern functions on supported backend/runtime paths. Input query functions SHALL be callable from authored code through the stdlib declarations, and backend tests SHALL verify button-state and axis-query behavior against backend runtime input state abstractions.

#### Scenario: Button query functions are backed
- **WHEN** authored code calls `pressed`, `down`, or `released` for an `InputButton`
- **THEN** the active backend resolves the call through a concrete runtime/backend-library implementation rather than a missing symbol

#### Scenario: Axis query functions are backed
- **WHEN** authored code calls `axis(a)` or `axis2(x, y)`
- **THEN** the active backend resolves the call through a concrete runtime/backend-library implementation rather than a missing symbol

#### Scenario: Input extern coverage is behaviorally verified
- **WHEN** the backend/runtime test suite runs
- **THEN** it includes tests covering button state queries and axis composition behavior for the declared input extern functions
