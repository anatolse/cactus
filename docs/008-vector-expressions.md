# Proposal 008: Expressive Vector and Quaternion Expressions

Status: mixed. The `vec2`/`vec3` operators, scalar splat constructors, and
compound assignment below are implemented and normatively specified in
`openspec/specs/dsl-vector-expressions/spec.md`. The quaternion portion — the
`quat * quat`/`quat * vec3` operator rows in the fixed operator matrix, the
"Quaternion composition order" note, and `rotation *= delta_rotation` in the
compound-assignment example — was superseded by proposal 012's named-API
decision (`quat.compose`, `quat.rotate_local`, `quat.rotate_world`) and was
never implemented; no quaternion operators exist. See
`docs/012-quaternion-rotation-semantics.md`.  
Kind: type system, operators, constructors, and expression lowering  
Semantic change: additional fixed typed operations  
Function overloading: explicitly out of scope

## Summary

Make vector-heavy gameplay code resemble its mathematical form without adding
general function overloading to Cactus.

Add:

- scalar splat constructors for `vec2` and `vec3`;
- a fixed matrix of vector, scalar, and quaternion operators;
- `+=`, `-=`, `*=`, and `/=` on writable fields and vector components.

Keep vector functions dimension-specific in `std.math.vec2` and
`std.math.vec3`.

## Design decision: no general overload resolution

This proposal does not allow multiple user or stdlib functions with the same
canonical name.

General overload resolution would require:

- symbol tables containing candidate sets instead of one canonical symbol;
- candidate collection across modules and imports;
- conversion and ranking rules;
- ambiguity diagnostics;
- signature-aware external callback resolution;
- additional module artifact and CIR identity rules;
- later interaction with generics and default arguments.

That complexity is not justified by a small vector API. Cactus keeps exact,
predictable function name resolution.

Fixed operator typing is different: the semantic analyzer already knows the
operand types of a `BinaryExpr` and can validate them against a closed table.
There is no candidate collection, coercion ranking, or ambiguity.

## Proposed constructors

Existing component constructors remain valid:

```cactus
vec2(x, y)
vec3(x, y, z)
```

Add scalar splat forms:

```cactus
vec2(0.0)       # vec2(0.0, 0.0)
vec3(scale)     # vec3(scale, scale, scale)
```

Accepted signatures are fixed compiler-known constructor forms:

```text
vec2(float)                -> vec2
vec2(float, float)         -> vec2
vec3(float)                -> vec3
vec3(float, float, float)  -> vec3
```

This is constructor arity checking, not general function overloading.

`quat.identity()` remains the canonical identity constructor. A quaternion
splat constructor is not introduced.

## Fixed operator matrix

| Left | Operator | Right | Result | Meaning |
| --- | --- | --- | --- | --- |
| `vec2` | `+` | `vec2` | `vec2` | component addition |
| `vec3` | `+` | `vec3` | `vec3` | component addition |
| `vec2` | `-` | `vec2` | `vec2` | component subtraction |
| `vec3` | `-` | `vec3` | `vec3` | component subtraction |
| `vec2` | `*` | `float` | `vec2` | scalar multiplication |
| `vec3` | `*` | `float` | `vec3` | scalar multiplication |
| `float` | `*` | `vec2` | `vec2` | scalar multiplication |
| `float` | `*` | `vec3` | `vec3` | scalar multiplication |
| `vec2` | `/` | `float` | `vec2` | scalar division |
| `vec3` | `/` | `float` | `vec3` | scalar division |
| `vec2` | `*` | `vec2` | `vec2` | component multiplication |
| `vec3` | `*` | `vec3` | `vec3` | component multiplication |
| `quat` | `*` | `quat` | `quat` | quaternion composition — **superseded, not implemented; see below** |
| `quat` | `*` | `vec3` | `vec3` | rotate vector — **superseded, not implemented; see below** |

Do not interpret `vec * vec` as a dot product. Dot product remains explicitly
named and dimension-qualified.

**Superseded.** Proposal 012 rejected `quat * quat` and `quat * vec3` in favor
of named calls (`quat.compose`/`quat.rotate_local`/`quat.rotate_world` and
`quat.rotate`) that make the reference frame and composition order explicit
at the call site instead of relying on a memorized operator convention.
Neither operator was implemented; `quat.rotate(rotation, value)` is the
existing named equivalent of the `quat * vec3` row above. The paragraph below
is retained for historical context only:

Quaternion composition order must be specified independently of the backend.
Recommended rule: `a * b` applies `b` first and then `a`, matching the existing
`quat.multiply(a, b)` behavior.

## Compound assignment

Support `+=`, `-=`, `*=`, and `/=` when the corresponding binary operation is
valid and the target is writable:

```cactus
motion.velocity.y -= motion.gravity * tick.dt
transform.position += motion.velocity * tick.dt
transform.scale *= growth
rotation *= delta_rotation  # superseded, not implemented — see the Status note above
```

`rotation *= delta_rotation` is not implemented: `quat` compound assignment
needs `quat * quat` first, which proposal 012 rejected outright — see
"Quaternion compound assignment" in `docs/012-quaternion-rotation-semantics.md`.
Use `rotation = quat.rotate_local(rotation, delta_rotation)` or
`quat.rotate_world(rotation, delta_rotation)` instead.

The target may be:

- a writable handler-local `var`;
- a writable selected trait field;
- a writable vector component reached through a selected trait alias.

Compound assignment through pair bindings remains rejected because durable
pair-bound trait access is read-only.

## Dimension-specific math modules remain canonical

```cactus
use std.math.vec2 as v2
use std.math.vec3 as v3

let distance_2d = v2.length(delta_2d)
let normal_2d = v2.normalize(delta_2d)
let closing_2d = v2.dot(relative_velocity_2d, normal_2d)

let distance_3d = v3.length(delta_3d)
let normal_3d = v3.normalize(delta_3d)
```

The existing module-qualified names remain unique canonical function symbols:

```text
std.math.vec2.length
std.math.vec2.normalize
std.math.vec2.dot
std.math.vec3.length
std.math.vec3.normalize
std.math.vec3.dot
```

No change to ordinary function lookup is required.

## Example

```cactus
use std.math.vec2 as v2

let delta = b.position - a.position
let distance = v2.length(delta)

if distance < a.radius + b.radius:
    let normal = delta / distance
    let relative_velocity = a.velocity - b.velocity

    if v2.dot(relative_velocity, normal) > 0.0:
        let impulse = compute_impulse(a, b, normal)
        emit BubbleBounce to a:
            new_velocity = a.velocity + normal * impulse
```

Most expression noise disappears through operators. The remaining `v2`
qualification makes the dimension explicit and keeps symbol resolution simple.

## Rejected alternatives

### User-defined function overloading

Rejected for this proposal. Its language-wide cost is disproportionate to the
vector readability benefit.

### Generic vector functions

Functions such as `length[T](value: T)` would require a generics design and are
not introduced as a hidden solution to overloading.

### Built-in methods

Syntax such as `delta.length()` would require method lookup on primitive types
and interact with existing dotted module/member syntax. It is not simpler
internally than keeping dimension-specific modules.

### Dimension suffixes

Names such as `math.length2` and `math.length3` avoid overloading but are less
clear than the existing `v2.length` and `v3.length` module aliases.

## Deferred optional intrinsic registry

If dimension qualification later proves to be substantial authoring noise, a
separate proposal may define a closed compiler-owned intrinsic registry:

```text
(std.math.length, [vec2])       -> Vec2Length
(std.math.length, [vec3])       -> Vec3Length
(std.math.dot, [vec2, vec2])    -> Vec2Dot
(std.math.dot, [vec3, vec3])    -> Vec3Dot
```

Such a registry must use exact argument `TypeKind` matching only:

- no implicit conversions;
- no candidate ranking;
- no ambiguity;
- no user-defined entries;
- one resolved intrinsic ID before CIR construction.

This registry is explicitly not part of the current proposal.

## Error semantics

- Mixed `vec2`/`vec3` operations are compile-time errors.
- Vector division requires a `float` divisor.
- Division by zero follows the documented backend-independent floating-point
  policy.
- `normalize(vec2(0.0))` and `normalize(vec3(0.0))` must have one documented
  backend-independent result. Recommended result: zero vector.
- Integer-to-vector and integer-to-float implicit conversions are not added.
- Unsupported compound assignments report the target type, operator, and
  source expression type.

## Compatibility

Existing component constructors and dimension-specific stdlib functions remain
valid. Existing explicit component arithmetic remains valid. This proposal adds
shorter equivalent expressions without changing function identity rules.

## Implementation work

- Lexer/parser: add `*=` and `/=` tokens if absent.
- Parser: accept scalar splat constructor arities.
- Semantic analysis: implement an exact operator matrix keyed by left type,
  operator, and right type.
- Assignment validation: support nested writable component paths.
- Stdlib: retain `std.math.vec2`, `std.math.vec3`, and `std.math.quat` unique
  function names.
- CIR: emit canonical typed unary/binary operations; do not carry unresolved
  operator candidates.
- C++ backend: use explicit raymath/runtime helpers rather than depending on
  accidental C++ operator availability.
- Other backends: implement identical quaternion order, zero normalization,
  and division behavior.
- Tests: every valid operator row, invalid dimension mixes, splat arities,
  nested assignments, pair write rejection, and backend equivalence.

## Acceptance criteria

- Position integration compiles as `position += velocity * dt`.
- `vec3(scale)` produces a three-component splat.
- Quaternion composition uses `a * b` with documented order.
- `v2.length` and `v3.length` retain distinct canonical symbol identities.
- The semantic analyzer contains no general overload candidate ranking.
- User functions with duplicate canonical names remain rejected.
- Unsupported mixed-dimension expressions fail with precise diagnostics.
- Backends agree on quaternion order, zero normalization, and division policy.

## CIR impact

Use typed expression nodes only. Splat constructors and compound assignments
may desugar to ordinary constructor, binary, and assignment nodes. Vector and
quaternion operators must be resolved to canonical typed operations before CIR
serialization. Ordinary function calls keep their existing unique resolved
symbol identity.

