## ADDED Requirements

### Requirement: Recognized spatial-overlap `where:` predicates are broad-phase eligible
A pair rule's `where:` predicate SHALL be eligible for broad-phase acceleration by a conforming
backend when both pair bindings require identical trait sets and the predicate matches one of two
recognized shapes, resolved by canonical identity so an aliased import is recognized the same as an
unaliased one:

- a direct, unwrapped call to a recognized spatial-overlap function
  (`std.collision.flat.circles_overlap`, `std.collision.volume.spheres_overlap`) whose position and
  radius arguments are each a member-chain rooted at one of the rule's two pair bindings; or
- an equivalent manual expression: a `<` or `<=` comparison whose left side is a dot product of the
  same position delta with itself (`dot(b.position - a.position, b.position - a.position)`) and
  whose right side is the square of the two bindings' summed radii (`(a.radius + b.radius) *
  (a.radius + b.radius)`), with every position/radius operand a member-chain rooted at one of the
  rule's two pair bindings.

Recognition is purely an optimization: it never changes which tuples satisfy the rule, every other
predicate shape remains fully supported as an ordinary predicate evaluated exactly as authored, and
no backend is required to implement the acceleration.

#### Scenario: Direct call to a recognized predicate is eligible
- **WHEN** a pair rule's `where:` clause calls `circles_overlap(a.tf.WorldTransform.position, a.Collider.radius, b.tf.WorldTransform.position, b.Collider.radius)` and both bindings require identical trait sets
- **THEN** the predicate is recognized as broad-phase eligible

#### Scenario: Aliased import is recognized identically
- **WHEN** the same call is written through an aliased import (`use std.collision.volume as foo`, calling `foo.spheres_overlap(...)`)
- **THEN** the predicate is recognized as broad-phase eligible, matched by resolved canonical identity rather than the spelling used at the call site

#### Scenario: Manual squared-distance expression is recognized
- **WHEN** a pair rule's `where:` clause is written as `v2m.dot(b.tf.WorldTransform.position - a.tf.WorldTransform.position, b.tf.WorldTransform.position - a.tf.WorldTransform.position) < (a.Collider.radius + b.Collider.radius) * (a.Collider.radius + b.Collider.radius)` and both bindings require identical trait sets
- **THEN** the predicate is recognized as broad-phase eligible, equivalently to calling `circles_overlap` directly

#### Scenario: Component-wise or let-bound expressions remain unrecognized
- **WHEN** a manual distance-vs-radius-sum check is written as separate squared-component arithmetic (`dx*dx + dy*dy`) or split across intermediate `let` bindings rather than the single dot-product comparison shape above
- **THEN** the predicate is evaluated exactly as authored, as an ordinary (non-accelerated) predicate, with no compile error

#### Scenario: Mismatched trait sets are not eligible
- **WHEN** a pair rule's two bindings require different trait sets, even if the `where:` predicate otherwise matches a recognized shape
- **THEN** the predicate is evaluated as an ordinary (non-accelerated) predicate

#### Scenario: Wrapped or combined predicates remain ordinary
- **WHEN** a recognized call or expression shape is wrapped in `not`, combined with `or`, or given a computed (non-member-chain) argument
- **THEN** the predicate is evaluated as an ordinary (non-accelerated) predicate, with no compile error

### Requirement: Unaccelerated linear-distance `where:` predicates produce a warning diagnostic
When a pair rule's `where:` predicate calls the unaccelerated linear-distance function
(`std.math.vec2.distance`, `std.math.vec3.distance`) with two pair-binding-rooted position
member-chain arguments, and compares the result with `<`, `<=`, `>`, or `>=` against a sum of two
pair-binding-rooted radius-like member-chain reads, the compiler SHALL emit a warning diagnostic
naming the recognized alternative (`circles_overlap`/`spheres_overlap`, or the equivalent squared
dot-product expression) appropriate to the predicate's dimension. This diagnostic SHALL NOT be an
error and SHALL NOT change compilation output.

#### Scenario: Linear 2D distance-vs-radius-sum predicate is flagged
- **WHEN** a pair rule's `where:` clause reads `v2m.distance(a.tf.WorldTransform.position, b.tf.WorldTransform.position) < a.Collider.radius + b.Collider.radius`
- **THEN** compilation succeeds and emits a warning diagnostic pointing at `circles_overlap` for that `where:` predicate

#### Scenario: Linear 3D distance-vs-radius-sum predicate is flagged
- **WHEN** a pair rule's `where:` clause reads `v3m.distance(a.tv.WorldTransform.position, b.tv.WorldTransform.position) >= a.Collider.radius + b.Collider.radius`
- **THEN** compilation succeeds and emits a warning diagnostic pointing at `spheres_overlap` for that `where:` predicate

#### Scenario: Unrelated where: predicates produce no diagnostic
- **WHEN** a pair rule's `where:` clause is an entity-identity comparison such as `a != b`, or any predicate that does not call the linear-distance function between binding-rooted positions
- **THEN** no warning diagnostic is emitted for that predicate

#### Scenario: Already-accelerated predicates produce no diagnostic
- **WHEN** a pair rule's `where:` predicate matches either recognized broad-phase-eligible shape (direct call or manual squared-distance expression)
- **THEN** no warning diagnostic is emitted for that predicate
