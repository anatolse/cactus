# Proposal 012: Explicit Quaternion Rotation Semantics

Status: implemented. `compose`, `rotate_local`, `rotate_world`, `same_rotation`,
and the rest of the named API below ship in `stdlib/std/math/quat.cactus`, are
normatively specified in `openspec/specs/stdlib-math/spec.md` (the
`std.math.quat` requirements), and are behaviorally verified in
`tests/test_runtime_stdlib.cpp` (`[quat]` tag). The `multiply` compatibility
function is retained exactly as proposed — see "Compatibility API" below.
`docs/008-vector-expressions.md`'s quaternion-operator alternative
(`quat * quat`, `quat * vec3`, `rotation *= delta`) was superseded by the
named-API decision here and was never implemented; that document's vector
operators and constructors are a separate, independently implemented proposal.  
Kind: stdlib API, backend-independent math semantics, and readability  
Semantic change: clarified and extended quaternion operations  
Parser or overload changes: none

## Summary

Keep quaternion operations as uniquely named functions in `std.math.quat`.
Do not add `quat * quat`, `quat * vec3`, or quaternion compound assignment.

Add names that expose the reference frame and composition order:

```cactus
quat.compose(parent, local)
quat.rotate_local(current, delta)
quat.rotate_world(current, delta)
```

Retain the existing constructors, interpolation, vector rotation, direction
extraction, and inverse operations. Keep `quat.multiply(a, b)` for source
compatibility, but stop using it as the canonical operation in examples.

This proposal requires no function overloading and no new operator-resolution
rules. Every operation remains an ordinary call to one canonical symbol.

## Motivation

Quaternion multiplication is compact but its order is not visually obvious.
The current examples contain both orders:

```cactus
rotation = quat.multiply(rotation, delta)
rotation = quat.multiply(delta, rotation)
```

Those expressions are type-correct but mean different things. A reader must
remember the multiplication convention and reconstruct whether `delta` is in
local or world space.

Nested composition is harder to scan:

```cactus
rotation = quat.multiply(pitch_delta, quat.multiply(yaw_delta, rotation))
```

The stdlib should name the intent instead of asking every author to decode the
algebra.

## Coordinate and rotation conventions

Cactus defines one backend-independent 3D convention:

- right is local `+X`;
- up is local `+Y`;
- forward is local `-Z`;
- positive axis-angle rotation follows the right-hand rule;
- angles are expressed in radians;
- quaternions represent active rotations of vectors;
- `compose(outer, inner)` applies `inner` first and `outer` second.

In algebraic notation:

```text
compose(outer, inner) = outer × inner
rotate(compose(a, b), v) = rotate(a, rotate(b, v))
```

This order matches the existing Hamilton-product implementation of
`quat.multiply(a, b)` and must not depend on a backend library's naming or
operator convention.

## Proposed canonical API

```cactus
module std.math.quat

pub extern func identity() quat
pub extern func from_euler(pitch: float, yaw: float, roll: float) quat
pub extern func from_axis_angle(axis: vec3, angle: float) quat

pub extern func compose(outer: quat, inner: quat) quat
pub extern func rotate_local(current: quat, delta: quat) quat
pub extern func rotate_world(current: quat, delta: quat) quat

pub extern func rotate(rotation: quat, value: vec3) vec3
pub extern func forward(rotation: quat) vec3
pub extern func right(rotation: quat) vec3
pub extern func up(rotation: quat) vec3

pub extern func normalize(value: quat) quat
pub extern func inverse(value: quat) quat
pub extern func slerp(from: quat, to: quat, t: float) quat
pub extern func same_rotation(a: quat, b: quat, tolerance: float) bool

# Compatibility API; avoid in new gameplay examples.
pub extern func multiply(a: quat, b: quat) quat
```

All names and signatures are unique. The semantic analyzer resolves them like
the current `quat.identity` and `quat.forward` functions.

## Composition helpers

The three public composition names have exact definitions:

| Call | Definition | Intended meaning |
| --- | --- | --- |
| `compose(outer, inner)` | normalized `outer × inner` | apply `inner`, then `outer` |
| `rotate_local(current, delta)` | `compose(current, delta)` | rotate around the entity's current local axes |
| `rotate_world(current, delta)` | `compose(delta, current)` | rotate around world-space axes |

The names avoid boolean flags such as `rotate(current, delta, true)` and avoid
an enum argument whose meaning is far from the call site.

### Hierarchical transforms

Parent and local rotations compose explicitly:

```cactus
world.rotation = quat.compose(parent_world.rotation, local.rotation)
```

The local rotation is applied first; the parent's world rotation is applied
second. This replaces a readability-sensitive use of
`quat.multiply(parent_world.rotation, local.rotation)` without changing the
represented orientation when the inputs are valid unit rotations.

### Incremental local rotation

```cactus
let yaw_delta = quat.from_euler(0.0, yaw_speed * tick.dt, 0.0)
rotation = quat.rotate_local(rotation, yaw_delta)
```

### Incremental world rotation

The current nested form:

```cactus
rotation = quat.multiply(pitch_delta, quat.multiply(yaw_delta, rotation))
```

becomes an ordered sequence:

```cactus
rotation = quat.rotate_world(rotation, yaw_delta)
rotation = quat.rotate_world(rotation, pitch_delta)
```

The two assignments expose both the order and the reference frame. No special
statement or pipeline syntax is needed.

### Local pitch, world yaw together

`rotate_local` and `rotate_world` only diverge once `current` and `delta`
rotate about different axes — rotations that share an axis commute, so
`compose(current, delta)` and `compose(delta, current)` land on the same
orientation whenever `current` and `delta` both rotate about that one axis.
The common case where they genuinely differ is a first-person camera: pitch
turns around the camera's own local `+X`, which tilts as the camera turns,
while yaw turns around the fixed world `+Y`:

```cactus
let pitch_delta = quat.from_euler(pitch_speed * tick.dt, 0.0, 0.0)
let yaw_delta = quat.from_euler(0.0, yaw_speed * tick.dt, 0.0)

rotation = quat.rotate_local(rotation, pitch_delta)
rotation = quat.rotate_world(rotation, yaw_delta)
```

`rotate_local(rotation, pitch_delta)` reinterprets `pitch_delta` through
`rotation`'s own current axes, so pitching always tilts around wherever the
camera is presently facing sideways. `rotate_world(rotation, yaw_delta)`
applies `yaw_delta` around the fixed world axis no matter how far the camera
has pitched, keeping the camera upright instead of drifting with its tilt.
Using the other function for either delta — `rotate_world` for pitch, or
`rotate_local` for yaw — produces a different, and for a camera, wrong,
orientation once `rotation` is no longer identity. Both functions normalize
their result, so `rotation` stays a unit quaternion across an unbounded
number of frames with no separate `quat.normalize` call.

No maintained example currently combines `rotate_local` and `rotate_world`
in one rule the way this snippet does; each uses one function for a single
accumulated delta. `examples/split-screen-forest-bombs/forest_bombs.cactus`'s
`MovePlayer` rule applies one yaw delta with `rotate_local` (turning the
player around its own up axis). `examples/mesh-renderer/main.cactus`'s
`ApplyScreenSpaceRotation` rule applies both a yaw and a pitch delta with
`rotate_world`, using axes read back from the object's current orientation
(`quat.up`/`quat.right`) rather than fixed world axes — a screen-space
"arcball" rotation, not the fixed-world-yaw convention above.
`examples/first-person-arena/main.cactus`'s `ApplyCameraPose` rule reaches
the fixed-world-yaw/local-pitch orientation this section describes, but by
recomputing
`quat.compose(quat.from_euler(0.0, pose.yaw, 0.0), quat.from_euler(pose.pitch, 0.0, 0.0))`
from accumulated yaw/pitch scalars each frame rather than composing deltas
incrementally.

**Caveat on the main spec's composition scenario.** `openspec/specs/stdlib-math/spec.md`'s
"Local and world incremental rotation are observably different" scenario says
`rotate_local(current, delta)` and `rotate_world(current, delta)` differ for
any non-identity `current` and `delta`. That holds for the axis-mismatched
pitch/yaw pair above, but not in general — two rotations about the same axis
commute, so `rotate_local` and `rotate_world` return the same quaternion for
them. Narrowing that scenario's wording is normative spec work, out of scope
for this documentation-only change (`skip_specs`); it is recorded as a
follow-up rather than changed here.

## Mechanical migration

Existing uses can be classified by intent rather than by syntax alone:

| Existing expression | Preferred expression | Use case in the reviewed code |
| --- | --- | --- |
| `multiply(parent, local)` | `compose(parent, local)` | hierarchical transforms |
| `multiply(current, delta)` | `rotate_local(current, delta)` | local player yaw |
| `multiply(delta, current)` | `rotate_world(current, delta)` | world-axis mesh rotation |
| `multiply(ry, rx)` | `compose(ry, rx)` | compound authored orientation |

Nested multiplication should first be expanded in application order and then
migrated one step at a time. A blind search-and-replace cannot infer the
reference frame from argument names in arbitrary user code.

## Constructor semantics

### Identity

`identity()` returns exactly:

```cactus
quat(0.0, 0.0, 0.0, 1.0)
```

Gameplay examples should use `identity()` instead of spelling the four raw
components.

### Axis-angle

`from_axis_angle(axis, angle)` normalizes `axis` before construction. If the
axis has exactly zero length, it returns `identity()`.

This replaces the current precondition that every caller must supply a unit
axis. It also makes the behavior deterministic across backends.

### Euler angles

`from_euler(pitch, yaw, roll)` uses intrinsic names tied to axes:

- `pitch`: rotation around `+X`;
- `yaw`: rotation around `+Y`;
- `roll`: rotation around `+Z`.

Its exact definition is:

```text
from_euler(pitch, yaw, roll)
    = compose(roll_z, compose(yaw_y, pitch_x))
```

Therefore pitch is applied first, then yaw, then roll. This preserves the
current C++ runtime convention while making it part of the Cactus contract.

## Normalization and total behavior

Because `quat(x, y, z, w)` can create a non-unit or zero quaternion, every
stdlib function needs defined behavior:

- `normalize(q)` returns a unit quaternion;
- `normalize(quat(0.0, 0.0, 0.0, 0.0))` returns `identity()`;
- named rotation constructors (`from_euler` and `from_axis_angle`), `compose`,
  `rotate_local`, `rotate_world`, and `slerp` return normalized quaternions;
- `rotate`, `forward`, `right`, and `up` normalize their quaternion input;
- `inverse(q)` computes the mathematical inverse for non-zero `q` and returns
  `identity()` for the zero quaternion;
- no operation produces NaN solely because a quaternion has zero length.

Normalization is observable work, so backends must not omit it based on an
assumption that inputs are already unit quaternions.

## Interpolation

`slerp(from, to, t)` has one predictable gameplay-oriented contract:

- clamp `t` to `[0.0, 1.0]`;
- normalize both inputs;
- choose the shortest rotational path by negating one input when their dot
  product is negative;
- return a normalized result;
- if both inputs are zero quaternions, return `identity()`.

Extrapolation is not hidden inside `slerp`. If it is needed later, add a
separately named `slerp_unclamped` function.

## Rotation equality

Do not use component equality to compare orientations. For every unit
quaternion `q`, `q` and `-q` represent the same rotation.

```cactus
if quat.same_rotation(actual, expected, 0.00001):
    emit RotationReached
```

`same_rotation(a, b, tolerance)`:

- rejects a negative `tolerance` at runtime by returning `false`;
- normalizes both values using the rules above;
- compares `abs(dot(a, b))` with `1.0 - tolerance`;
- therefore treats `q` and `-q` as the same orientation.

The ordinary `==` operator is not added for `quat` by this proposal.

## Compatibility API

`quat.multiply(a, b)` remains available and preserves its current raw Hamilton
product:

```text
multiply(a, b) = a × b
```

It does not normalize the result. That detail preserves existing behavior and
makes it suitable for low-level interoperability. New stdlib code, examples,
and documentation should prefer `compose`, `rotate_local`, or `rotate_world`.

Maintained examples already use only the named API — compound authored
orientation (composing two independently tracked deltas, e.g. yaw and pitch)
in `examples/first-person-arena/main.cactus` (`ApplyCameraPose`) and
`examples/waving-label-3d/waving_label_3d.cactus`; local/world incremental
rotation in `examples/mesh-renderer/main.cactus`, `examples/model-animation/main.cactus`,
and `examples/split-screen-forest-bombs/forest_bombs.cactus`. None of them
call raw `multiply`, so no example needed migrating for this change; none
currently demonstrates the parent/local `compose(parent, local)` pattern from
"Mechanical migration" above.

A future deprecation can be considered only after existing users have a
mechanical migration path.

## Rejected alternatives

### `quat * quat`

Rejected because it hides the most error-prone fact: which rotation is applied
first. Fixed operator typing would be easy for the semantic analyzer, but the
resulting source would be less explicit than the named API.

### `quat * vec3`

Rejected because `quat.rotate(rotation, value)` is already concise and exposes
which argument is the rotation.

### Quaternion compound assignment

`rotation *= delta` is rejected because it cannot say whether `delta` is local
or world-space. Use `rotate_local` or `rotate_world`.

### General function overloading

Rejected as unnecessary. The proposed functions have distinct canonical names
and exact signatures.

### Backend-native Euler and multiplication conventions

Rejected because they would make identical Cactus source behave differently
across backends.

## Lowering and CIR impact

No new expression node or CIR instruction is required.

- Calls resolve to ordinary canonical symbols such as
  `std.math.quat.rotate_local`.
- `compose`, `rotate_local`, and `rotate_world` remain calls in CIR.
- No unresolved operator or overload candidate reaches CIR.
- A backend may inline a helper only after preserving its specified
  normalization and order.

## Implementation work

- Stdlib: add the new declarations to `std.math.quat`; retain `multiply`.
- Runtime: implement normalized `compose`, local/world helpers, `normalize`,
  and `same_rotation`.
- Runtime: make constructor, inverse, vector rotation, direction extraction,
  and slerp behavior match the total rules above.
- Transform stdlib/backend: replace parent/local `multiply` calls with
  `compose`.
- Examples: migrate intent-sensitive `multiply` calls to `compose`,
  `rotate_local`, or `rotate_world`.
- Documentation: state axes, handedness, Euler order, units, and slerp clamping
  beside the public declarations.
- Tests: use known rotations and basis vectors rather than comparing raw
  quaternion signs.

The parser and ordinary semantic function lookup require no changes.

## Acceptance criteria

- Parent/local transform composition is written as
  `quat.compose(parent, local)` and preserves the current orientation for unit
  inputs.
- Local and world incremental rotations have distinct readable calls and
  produce observably different results for a non-identity `current` rotation.
- `from_euler` has backend-independent axis and order tests.
- `forward(identity())`, `right(identity())`, and `up(identity())` return
  local `-Z`, `+X`, and `+Y` respectively.
- Axis-angle construction accepts a non-unit axis and returns a unit
  quaternion.
- All zero-quaternion cases follow the documented total behavior without NaN.
- `slerp` clamps `t`, follows the shortest path, and returns a unit quaternion.
- `same_rotation(q, -q, tolerance)` returns `true` for a valid tolerance.
- Existing source using `quat.multiply` still compiles unchanged.
- The semantic analyzer contains no general overload resolution and no
  quaternion-specific operator table.
- All backends pass the same quaternion conformance suite.
