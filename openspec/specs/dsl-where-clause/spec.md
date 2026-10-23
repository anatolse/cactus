# dsl-where-clause Specification

## Purpose
Define the `where:` clause: a pure boolean predicate list that restricts a regular rule's unary (`each:`) or pair (`pairs:`) domain before its handler executes, independent of any particular execution strategy.
## Requirements
### Requirement: `where:` restricts an existing unary or pair domain
A regular rule that declares `each:` and/or `pairs:` SHALL accept an optional `where:` block containing one or more boolean predicate expressions. Multiple predicate lines SHALL form an unordered logical conjunction: every line MUST evaluate to `true` for the entity or tuple to remain in the domain. `where:` SHALL be rejected on rules with neither `each:` nor `pairs:` and on `extern rule` declarations.

#### Scenario: where: restricts a unary filter domain
- **WHEN** a rule declares `each: Ball as ball` and `where: ball.velocity.x > 0.0`
- **THEN** the handler executes only for entities satisfying both the filter and the predicate

#### Scenario: where: restricts a pair domain
- **WHEN** a rule declares `pairs:` bindings `a`/`b` and `where: a != b`
- **THEN** the handler executes only for tuples satisfying both the pair domain and the predicate

#### Scenario: Multiple predicate lines form a conjunction
- **WHEN** `where:` contains two lines, `a != b` and `distance(a, b) < 1.0`
- **THEN** a tuple executes only when both lines evaluate to `true`

#### Scenario: where: without filter or pairs is rejected
- **WHEN** a rule declares `where:` but neither `each:` nor `pairs:`
- **THEN** compilation reports that `where:` requires an existing unary or pair domain

### Requirement: Every `where:` predicate type-checks as bool
Each `where:` predicate expression SHALL have static type `bool`.

#### Scenario: Non-bool predicate is rejected
- **WHEN** a `where:` line evaluates to a non-`bool` type such as `float` or `entity_id`
- **THEN** semantic analysis reports a type error for that predicate

### Requirement: `where:` predicates SHALL be pure
A `where:` expression MAY contain literals and constants, filter/pair-binding reads, entity identity comparisons, arithmetic and boolean operators, and calls to functions whose complete call graph is proven pure. A `where:` expression MUST NOT mutate traits; emit events; spawn or destroy entities; add, remove, or project traits; access input, time, logging, audio, or other external effects; execute world queries; or call a function whose effects are opaque or unknown.

#### Scenario: Pure user function call is accepted
- **WHEN** a `where:` predicate calls a `func` whose entire call graph performs only pure arithmetic
- **THEN** the predicate is accepted

#### Scenario: Emit in where: is rejected
- **WHEN** a `where:` predicate's expression contains an `emit`
- **THEN** semantic analysis reports that `where:` predicates must be pure

#### Scenario: World query in where: is rejected
- **WHEN** a `where:` predicate calls a world query such as `query.first()`
- **THEN** semantic analysis reports that `where:` predicates must be pure

#### Scenario: Structural command in where: is rejected
- **WHEN** a `where:` predicate's expression contains `spawn`, `destroy`, `add`, `remove`, or `project`
- **THEN** semantic analysis reports that `where:` predicates must be pure

#### Scenario: Call to a function with unknown or non-empty effects is rejected
- **WHEN** a `where:` predicate calls an extern function whose semantic contract does not guarantee purity, or a `func` whose call graph reaches such a function
- **THEN** semantic analysis reports that `where:` predicates must be pure

### Requirement: `where:` evaluation order is unspecified
The compiler SHALL NOT guarantee an evaluation order or observable short-circuit behavior for `where:` predicates, and MAY reorder, combine, remove, inline, or replace them with an equivalent restriction, since every predicate is pure and side-effect-free by construction.

#### Scenario: Predicate order does not affect the result
- **WHEN** two semantically independent `where:` lines are written in either order
- **THEN** the compiled program admits the same entities or tuples regardless of source order

### Requirement: `where:` evaluates once per pass, against the already-selected domain
`where:` SHALL be evaluated once per entity or tuple, at the start of that entity's or tuple's handler invocation, against the domain membership already determined by `each:`/`without:`/`pairs:`. `where:` MAY only remove entities or tuples from that membership; it MUST NOT add any and MUST NOT be re-evaluated mid-pass as a result of mutations, projections, or buffered structural commands performed by earlier invocations in the same pass. A predicate whose inputs no invocation of the pass can change (`expression-staging`) gives the same result at any point of the pass, so a backend MAY evaluate it once per activation or once per binding member, before any invocation; the entities or tuples that run, and their order, SHALL be the same as if it were evaluated at the start of each invocation.

#### Scenario: Rejected entities do not run the handler body
- **WHEN** an entity satisfies `each:` but not `where:`
- **THEN** its handler body does not execute for that activation

#### Scenario: where: cannot expand the pass
- **WHEN** an earlier invocation in the same pass performs a mutation that would make another entity newly satisfy a `where:` predicate
- **THEN** that other entity's membership for the current pass is unaffected

#### Scenario: Lifted predicate admits the same tuples
- **WHEN** a pair rule's `where:` has a predicate reading only one binding and a join predicate, and the pass is run with lifting and with every predicate evaluated per tuple
- **THEN** both runs execute the same tuples in the same order, including self-tuples

#### Scenario: Predicate whose input the pass writes sees live values
- **WHEN** a unary rule's `where:` reads a named entity field that its handler writes, and an earlier invocation of the pass changes that field
- **THEN** later invocations evaluate the predicate against the changed value

### Requirement: Unaccelerated linear-distance `where:` predicates produce a warning diagnostic
When a pair rule's `where:` predicate calls the unaccelerated linear-distance function
(`std.math.vec2.distance`, `std.math.vec3.distance`) with two pair-binding-rooted position
member-chain arguments, and compares the result with `<`, `<=`, `>`, or `>=` against a sum of two
pair-binding-rooted radius-like member-chain reads, the compiler SHALL emit a warning diagnostic
naming the recognized alternative (`circles_overlap`/`spheres_overlap`, or the equivalent squared
dot-product expression) appropriate to the predicate's dimension. This diagnostic SHALL NOT be an
error and SHALL NOT change compilation output.

#### Scenario: Linear 2D distance-vs-radius-sum predicate is flagged
- **WHEN** a pair rule's `where:` clause reads `v2m.distance(a[tf.WorldTransform].position, b[tf.WorldTransform].position) < a[Collider].radius + b[Collider].radius`
- **THEN** compilation succeeds and emits a warning diagnostic pointing at `circles_overlap` for that `where:` predicate

#### Scenario: Linear 3D distance-vs-radius-sum predicate is flagged
- **WHEN** a pair rule's `where:` clause reads `v3m.distance(a[tv.WorldTransform].position, b[tv.WorldTransform].position) >= a[Collider].radius + b[Collider].radius`
- **THEN** compilation succeeds and emits a warning diagnostic pointing at `spheres_overlap` for that `where:` predicate

#### Scenario: Unrelated where: predicates produce no diagnostic
- **WHEN** a pair rule's `where:` clause is an entity-identity comparison such as `a != b`, or any predicate that does not call the linear-distance function between binding-rooted positions
- **THEN** no warning diagnostic is emitted for that predicate

#### Scenario: Already-accelerated predicates produce no diagnostic
- **WHEN** a pair rule's `where:` predicate matches either recognized broad-phase-eligible shape (direct call or manual squared-distance expression)
- **THEN** no warning diagnostic is emitted for that predicate

### Requirement: Recognized overlap `where:` predicates are broad-phase eligible across any two bindings
A pair rule's `where:` predicate SHALL be eligible for broad-phase acceleration by a conforming
backend when it matches one of the recognized shapes below, whatever trait sets the two pair
bindings require. Recognition SHALL resolve functions by canonical identity, so an aliased
import is recognized the same as an unaliased one:

- a direct, unwrapped call to a recognized overlap function:
  - `std.collision.flat.circles_overlap(a_position, a_radius, b_position, b_radius)`
  - `std.collision.volume.spheres_overlap(a_position, a_radius, b_position, b_radius)`
  - `std.collision.volume.sphere_box_overlap(sphere_position, sphere_radius, box_position, box_size, box_rotation)`

  where the arguments describing one shape (the `a_*` arguments, the `b_*` arguments, the
  `sphere_*` arguments, or the `box_*` arguments) reference exactly one pair binding between
  them, and the two shapes reference different pair bindings. Either binding MAY supply either
  shape. Each shape argument SHALL be a pure expression whose pair-binding references all root
  at that shape's binding; it MAY also use literals, constants, named entity field reads and
  pure function calls, and it MAY reference no pair binding at all (for example a constant
  radius). A shape whose arguments reference no pair binding, or both bindings, makes the call
  unrecognized; or
- an equivalent manual expression: a `<` or `<=` comparison whose left side is a dot product of the
  same position delta with itself (`dot(b.position - a.position, b.position - a.position)`) and
  whose right side is the square of the two bindings' summed radii (`(a.radius + b.radius) *
  (a.radius + b.radius)`), with every position/radius operand a member-chain rooted at one of the
  rule's two pair bindings.

Recognition is purely an optimization: it never changes which tuples satisfy the rule or the
order in which they run, every other predicate shape remains fully supported as an ordinary
predicate evaluated exactly as authored, and no backend is required to implement the
acceleration.

#### Scenario: Direct call to a recognized predicate is eligible
- **WHEN** a pair rule's `where:` clause calls `circles_overlap(a[tf.WorldTransform].position, a[Collider].radius, b[tf.WorldTransform].position, b[Collider].radius)`
- **THEN** the predicate is recognized as broad-phase eligible

#### Scenario: Aliased import is recognized identically
- **WHEN** the same call is written through an aliased import (`use std.collision.volume as foo`, calling `foo.spheres_overlap(...)`)
- **THEN** the predicate is recognized as broad-phase eligible, matched by resolved canonical identity rather than the spelling used at the call site

#### Scenario: Manual squared-distance expression is recognized
- **WHEN** a pair rule's `where:` clause is written as `v2m.dot(b[tf.WorldTransform].position - a[tf.WorldTransform].position, b[tf.WorldTransform].position - a[tf.WorldTransform].position) < (a[Collider].radius + b[Collider].radius) * (a[Collider].radius + b[Collider].radius)`
- **THEN** the predicate is recognized as broad-phase eligible, equivalently to calling `circles_overlap` directly

#### Scenario: Different trait sets are eligible
- **WHEN** a pair rule's bindings are `actor: Actor, tv.WorldTransform` and `wall: Solid, physics.BoxCollider, tv.WorldTransform`, and its `where:` calls `spheres_overlap` or `sphere_box_overlap` with one shape from each binding
- **THEN** the predicate is recognized as broad-phase eligible

#### Scenario: Sphere against box is eligible in either binding order
- **WHEN** a pair rule's `where:` calls `sphere_box_overlap(actor[tv.WorldTransform].position, actor[Actor].radius, wall[tv.WorldTransform].position, wall[physics.BoxCollider].size, wall[tv.WorldTransform].rotation)`, or the same call with the left binding supplying the box and the right binding supplying the sphere
- **THEN** the predicate is recognized as broad-phase eligible

#### Scenario: Single-binding expression argument is eligible
- **WHEN** a recognized call's radius argument is `a[Actor].radius + PROBE_DISTANCE`, where `PROBE_DISTANCE` is a constant
- **THEN** the predicate is recognized as broad-phase eligible

#### Scenario: Named entity read in an argument is eligible
- **WHEN** a recognized call's radius argument is `a[Actor].radius + Game[Tuning].margin`, where `Game` is a declared named entity
- **THEN** the predicate is recognized as broad-phase eligible

#### Scenario: Constant argument is eligible
- **WHEN** a recognized call's radius argument is the constant `BULLET_RADIUS` and its position argument is `bullet[tv.WorldTransform].position`
- **THEN** the predicate is recognized as broad-phase eligible, with the sphere supplied by `bullet`

#### Scenario: Argument mixing both bindings is not eligible
- **WHEN** a recognized call's radius argument is `a[Actor].radius + b[Actor].radius`
- **THEN** the predicate is evaluated as an ordinary (non-accelerated) predicate, with no compile error

#### Scenario: Both shapes from one binding are not eligible
- **WHEN** every argument of a recognized call references the same pair binding
- **THEN** the predicate is evaluated as an ordinary (non-accelerated) predicate, with no compile error

#### Scenario: Component-wise or let-bound expressions remain unrecognized
- **WHEN** a manual distance-vs-radius-sum check is written as separate squared-component arithmetic (`dx*dx + dy*dy`) or split across intermediate `let` bindings rather than the single dot-product comparison shape above
- **THEN** the predicate is evaluated exactly as authored, as an ordinary (non-accelerated) predicate, with no compile error

#### Scenario: Wrapped or combined predicates remain ordinary
- **WHEN** a recognized call or expression shape is wrapped in `not` or combined with `or`
- **THEN** the predicate is evaluated as an ordinary (non-accelerated) predicate, with no compile error

### Requirement: Unaccelerated pair rules produce a warning diagnostic
When a pair rule has no broad-phase-eligible predicate in its `where:` clause, no broad-phase-eligible collider aggregate (`first_hit` over a sweep, or `sum` over a push-out) in its `reduce:` clause, and no equality join, including a pair rule with neither clause, the compiler SHALL emit one warning diagnostic located at the rule's `pairs:` clause: `pair rule '<Rule>' is not accelerated: no recognized overlap predicate in where:, so every (<left>, <right>) tuple is checked`, where `<left>` and `<right>` are the binding names. When the existing linear-distance warning fires for one of the rule's predicates, it SHALL replace this warning, so the rule gets one warning. These warnings SHALL NOT be errors and SHALL NOT change compilation output.

#### Scenario: Pair rule without where: is flagged
- **WHEN** a pair rule `ComposePose` with bindings `rig` and `body` has no `where:` clause and no `reduce:` clause
- **THEN** compilation succeeds and emits the warning "pair rule 'ComposePose' is not accelerated: no recognized overlap predicate in where:, so every (rig, body) tuple is checked"

#### Scenario: Pair rule with only unrecognized predicates is flagged
- **WHEN** a pair rule's `where:` clause contains only `a != b`
- **THEN** compilation succeeds and emits the unaccelerated pair rule warning for that rule

#### Scenario: Accelerated pair rule is not flagged
- **WHEN** a pair rule's `where:` clause contains `a != b` and a broad-phase-eligible `spheres_overlap` call
- **THEN** no unaccelerated pair rule warning is emitted for that rule

#### Scenario: Swept first-hit rule is not flagged
- **WHEN** a pair rule has no `where:` clause and a broad-phase-eligible `first_hit(physics.sweep(...))` aggregate
- **THEN** no unaccelerated pair rule warning is emitted for that rule

#### Scenario: Push-out sum rule is not flagged
- **WHEN** a pair rule has no `where:` clause and a broad-phase-eligible `sum(physics.push_out(...))` aggregate
- **THEN** no unaccelerated pair rule warning is emitted for that rule

#### Scenario: Equality join rule is not flagged
- **WHEN** a pair rule reduces per one binding and its `where:` equates a `let` field of the other binding with a value of the group binding
- **THEN** no unaccelerated pair rule warning is emitted for that rule

#### Scenario: Linear-distance warning replaces the generic warning
- **WHEN** a pair rule's only distance test is `v3m.distance(a[tv.WorldTransform].position, b[tv.WorldTransform].position) < a[Collider].radius + b[Collider].radius`
- **THEN** compilation emits the linear-distance warning pointing at `spheres_overlap`
- **AND** does not emit the unaccelerated pair rule warning for that rule

#### Scenario: Unary rules are never flagged
- **WHEN** a rule uses `each:` rather than `pairs:`
- **THEN** no unaccelerated pair rule warning is emitted for it

### Requirement: Collider queries are broad-phase eligible without author-written bounds
A pair rule SHALL be eligible for broad-phase acceleration by a conforming backend when it uses any collider query form below, with its two arguments being the rule's two different pair bindings:

- a direct, unwrapped `where:` predicate `std.physics.volume.touching(a, b)`;
- a `reduce:` aggregate `first_hit(std.physics.volume.sweep(subject, delta, target))`, where `delta` is a pure expression that reads no pair binding other than `subject`, and every other aggregate in the same `reduce:` clause is also a `first_hit` over a sweep with the same `subject`, `delta`, and `target`; or
- a `reduce:` aggregate `sum(std.physics.volume.push_out(a, b))`, where every other aggregate in the same `reduce:` clause is also a `sum` over a push-out with the same `a` and `b`.

Other reducers count rows or sum values that pruned rows would change, so mixing them disables pruning. Recognition SHALL resolve functions by canonical identity. The acceleration bound SHALL come from the colliders themselves: each entity's shape and, for a sweep, the subject's motion over `delta`. The author SHALL NOT need any other predicate for acceleration. Recognition is purely an optimization: it SHALL NOT change which rows satisfy the rule, the aggregate values, or the order in which handlers run.

#### Scenario: touching is eligible on its own
- **WHEN** a pair rule's only `where:` predicate is `physics.touching(player, enemy)`
- **THEN** the rule is broad-phase eligible and no unaccelerated pair rule warning is emitted

#### Scenario: Swept first hit is eligible on its own
- **WHEN** a pair rule has no `where:` clause and reduces `first = first_hit(physics.sweep(bullet, bullet[Bullet].velocity * fixed_tick.dt, target))` per bullet
- **THEN** the rule is broad-phase eligible and no unaccelerated pair rule warning is emitted

#### Scenario: Push-out sum is eligible on its own
- **WHEN** a pair rule has no `where:` clause and reduces `push = sum(physics.push_out(body, other))` per body
- **THEN** the rule is broad-phase eligible and no unaccelerated pair rule warning is emitted

#### Scenario: Pruning does not change the result
- **WHEN** the same swept or push-out rule runs with and without acceleration over the same world
- **THEN** every group receives the same aggregate value

#### Scenario: Mixed reducers are not eligible
- **WHEN** a rule reduces both `first = first_hit(physics.sweep(bullet, step, target))` and `near = count()`
- **THEN** the sweep is evaluated as an ordinary aggregate and the unaccelerated pair rule warning is emitted

#### Scenario: Wrapped touching is not eligible
- **WHEN** `physics.touching(a, b)` is wrapped in `not` or combined with `or`
- **THEN** the predicate is evaluated as an ordinary predicate, with no compile error

### Requirement: Inline row predicates
A regular rule SHALL accept inline `where: predicate, predicate` as equivalent to its existing block predicate list. `each:` SHALL qualify as the same positive unary selection as `each:`. Boolean typing, purity, predicate evaluation rules, group/row classification during reduction, and spatial recognition SHALL remain unchanged. Inline predicates SHALL remain rejected in selectionless and extern rules under the existing domain restrictions.

#### Scenario: Inline predicates constrain the same rows
- **WHEN** a pair rule uses `where: touching(body, lava), body != lava` with a valid pure geometric function
- **THEN** it retains exactly the rows of the equivalent two-entry block, with the same acceleration decision

#### Scenario: Canonical unary selection supports where
- **WHEN** a rule uses `each: Motion as motion` and `where: motion.speed > 0.0`
- **THEN** it selects the same invocations as the corresponding `each:` rule

#### Scenario: Inline predicates retain purity and type errors
- **WHEN** an inline predicate is non-boolean or calls an effectful function
- **THEN** compilation rejects that predicate at its own source location

### Requirement: Two unrotated boxes are broad-phase eligible
A pair rule's `where:` predicate SHALL be eligible for broad-phase acceleration when it is a direct, unwrapped call to `std.collision.volume.boxes_overlap(a_center, a_size, b_center, b_size)`, resolved by canonical identity, whose `a_*` arguments reference exactly one pair binding and whose `b_*` arguments reference exactly the other, under the same argument rules as the other recognized overlap functions. `boxes_overlap` SHALL hold exactly when the two boxes share interior volume. The bound SHALL be each box's extent.

#### Scenario: A region selects the cells it covers
- **WHEN** a pair rule's `where:` calls `collision.boxes_overlap(region[Region].center, region[Region].size, cell[tv.WorldTransform].position, cell[physics.BoxCollider].size)`
- **THEN** the predicate is recognized as broad-phase eligible
- **AND** cells that only touch the region's faces are not selected

### Requirement: A ray segment against an unrotated box is broad-phase eligible
A pair rule's `where:` predicate SHALL be eligible for broad-phase acceleration when it is a direct, unwrapped call to `std.collision.volume.ray_hits_box(origin, direction, max_distance, box_center, box_size)`, resolved by canonical identity, where the ray arguments (`origin`, `direction`, `max_distance`) reference exactly one pair binding between them, the box arguments (`box_center`, `box_size`) reference exactly the other, and each argument follows the same purity and single-binding rules as the other recognized overlap functions. The bound SHALL come from the call itself: the segment from `origin` along `direction` for `max_distance`, and the box's extent. A segment that `ray_box` rejects outright SHALL bound nothing. Recognition SHALL NOT change which tuples satisfy the rule or their order.

#### Scenario: Ray probe against cells is eligible
- **WHEN** a pair rule's `where:` calls `collision.ray_hits_box(probe[RayProbe].origin, probe[RayProbe].direction, probe[RayProbe].max_distance, cell[tv.WorldTransform].position, cell[physics.BoxCollider].size)`
- **THEN** the predicate is recognized as broad-phase eligible and no unaccelerated pair rule warning is emitted

#### Scenario: A ray argument reading the box binding is not eligible
- **WHEN** the call's `direction` argument reads the binding that supplies the box
- **THEN** the predicate is evaluated as an ordinary predicate, with no compile error

### Requirement: Equality joins on values a member never changes are indexed
A pair rule that reduces per one binding SHALL be recognized as an equality join when the operands of the top-level `and` chains of its `where:` predicates include at least one `==` comparison whose one side is either the other (indexed) binding itself or a `let` field of type `int`, `bool`, enum or `entity_id` read through that binding, and whose other side reads nothing of the indexed binding. A conforming backend MAY then look up, for each group entity, only the indexed members whose values equal the group's values. When one of the keys is the indexed binding itself, the backend SHALL look that entity up directly for each group and SHALL NOT keep a persistent index of the binding's members; an entity that is not alive or does not carry the binding's traits yields no row. Every predicate SHALL still be evaluated for each looked-up row, rows SHALL fold in the same order as the full product, and the result SHALL be the same as without the index.

#### Scenario: Coordinate lookup uses the equality join
- **WHEN** a rule reduces per `probe` with `where: cell[Cell].volume == probe[CellProbe].volume and cell[Cell].x == probe[CellProbe].x`, and `Cell.volume` and `Cell.x` are `let` fields
- **THEN** the rule is an equality join on those two values and no unaccelerated pair rule warning is emitted

#### Scenario: Entity identity is a join key
- **WHEN** a rule reduces per `probe` with `where: cell == probe[RayProbe].cell`
- **THEN** the rule is an equality join on the cell's identity

#### Scenario: An identity join keeps no index
- **WHEN** a rule reduces per `probe` with `where: cell == probe[RayProbe].cell` over many cells
- **THEN** each pass checks only the entity each probe names, and no persistent index of the cells exists

#### Scenario: An identity join on a handle without the binding's traits
- **WHEN** a probe's handle names a live entity that lacks `Cell`, or an entity that was destroyed
- **THEN** that probe folds no row and its reductions take their defaults

#### Scenario: Mutable fields are not join keys
- **WHEN** the only equality compares a `var` field of the indexed binding
- **THEN** the rule is not an equality join and the unaccelerated pair rule warning is emitted

### Requirement: Pair-binding indexes persist across passes without changing results
A conforming backend MAY keep one binding of a pair pass in an index that survives between passes, when that pass has a recognized spatial bound or an equality join: the non-group binding of a reduced pass, or the right binding of a plain pass, whose left snapshot is then walked in order so tuples keep the full product's order. Such an index SHALL be brought up to date with every committed membership change of the binding before a pass reads it, SHALL be rebuilt after a world is initialized or replaced by a restore, and SHALL recompute each pass the bounds of every member a handler may write after construction. A member counts as possibly written when it carries every trait a writing handler selects, or is a named entity a handler writes, judged from the whole program's handler contracts; native writers with unknown targets count as writing every carrier of the trait. Indexes SHALL NOT change which rows run, the aggregate values, or the order of handler effects, and the compiler SHALL offer a test-only switch that disables them so both paths can be compared. Indexes observe only the program's own writes and committed structural changes; a host that writes trait fields from native code SHALL advance the runtime's index epoch afterwards, which rebuilds every index before its next use.

#### Scenario: Static cells are indexed once
- **WHEN** a character body's solver passes run against many solid cells whose transforms and colliders no handler writes
- **THEN** each pass looks up only the cells near the body instead of rebuilding a broad phase over every cell

#### Scenario: A moving solid stays exact
- **WHEN** another character body, whose transform the solver writes, is a member of the same binding
- **THEN** its bounds are recomputed every pass and it collides exactly as without an index

#### Scenario: A region edit looks up only the cells it covers
- **WHEN** an event-triggered plain pair rule matches a dragged region against every cell of a large volume
- **THEN** the pass looks up the cells inside the region in the persistent index instead of building a broad phase over every cell

#### Scenario: Digging updates the index
- **WHEN** an edit removes `Solid` from a cell and the next pass runs
- **THEN** that pass no longer offers the cell as a candidate

#### Scenario: A host write is announced
- **WHEN** a custom host moves an entity by writing its transform directly and then advances the index epoch
- **THEN** the next pass sees the entity at its new place

#### Scenario: Indexed and unindexed builds agree
- **WHEN** the same program and inputs run once with indexes and once with the test-only switch disabling them
- **THEN** every observable result is identical

### Requirement: Index structures are chosen at compile time from the program's definitions
A conforming backend that keeps a persistent pair-binding index SHALL choose its storage structure at compile time from the program's definitions, without author annotations, and SHALL NOT switch between structures at runtime. Runtime values MAY only size the chosen structure. For an equality join, each key part SHALL use:
- for `enum` or `bool`, direct addressing over the type's values;
- for `entity_id`, addressing by entity index;
- for an `int` `let` field declared `in range(...)` against a container, dense addressing within each container, sized from the container's fields;
- for any other `int`, hashing, with an informational diagnostic that names the rule and the key part and says a declared range would allow dense addressing.

Spatial indexes over static members whose bounds come from the same code SHALL share one store when their member archetypes can overlap. Every choice SHALL keep the results, candidate order and handler effects of the unindexed pass, and the compiler SHALL offer test-only switches that force each alternative so they can be compared.

#### Scenario: Ranged coordinates get a dense index
- **WHEN** a pair rule equates `cell[Cell].x`, `.y` and `.z` with group values, and `Cell.x`, `.y` and `.z` are `let` fields declared in range of their volume
- **THEN** the backend indexes the cells densely within each volume and emits no diagnostic for that rule

#### Scenario: An unranged int key is hashed and reported
- **WHEN** a pair rule's equality key is an `int` `let` field with no declared range
- **THEN** the backend hashes that key and emits an informational diagnostic naming the rule and the field

#### Scenario: Same-shaped spatial indexes share a store
- **WHEN** three pair rules index solid cells, all cells and bedrock cells through the same box-collider bounds
- **THEN** one static store holds each cell once, and each rule sees only its own members

#### Scenario: Mixed collider sizes are indexed exactly
- **WHEN** a level's static walls range from 0.5 to 29.5 units long and a body's pass queries them
- **THEN** every wall overlapping the query is a candidate exactly as without an index

#### Scenario: Forced alternatives agree
- **WHEN** the same program and inputs run with the chosen structures, with each forced alternative, and without indexes
- **THEN** every observable result is identical

#### Scenario: A full voxel world indexes compactly
- **WHEN** the 196,608-cell robot sandbox has built every binding index its rules use
- **THEN** all binding indexes together occupy at most 16 MB
