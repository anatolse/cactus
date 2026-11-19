# handler-contracts Specification

## Purpose
Define per-handler selection, inferred and declared access/effect contracts, contract validation, and selectionless execution semantics for regular and external handlers.

## Requirements

### Requirement: Contracts belong to individual handlers
Every regular or external handler SHALL have an independent contract containing its trigger, selection, reads, writes, emitted events, structural commands, and external effect domains. A rule-level filter/exclude/order definition SHALL be copied into each handler node but SHALL NOT merge the handlers' access contracts.

#### Scenario: Multi-handler rule has distinct contracts
- **WHEN** `Player` handles fixed_tick and Damaged with different behavior
- **THEN** the semantic representation contains distinct contracts for `Player.fixed_tick` and `Player.Damaged`

### Requirement: Handler contracts declare one execution domain

Every handler contract SHALL contain exactly one domain: selectionless, unary relation, or pair relation. A unary entity relation SHALL contain positive and excluded canonical trait identities; a unary row domain SHALL contain its canonical relation identity and alias. A pair relation SHALL contain exactly two ordered named bindings. Each binding SHALL record either its required canonical entity traits or its canonical row relation identity. A pair of row bindings SHALL retain both identities and names even when they refer to the same relation. Physical join order SHALL NOT change the recorded authored binding order.

#### Scenario: Unary contract remains unary
- **WHEN** a rule filters Position and Velocity
- **THEN** each handler contract records one unary relation rather than a pair or selectionless domain

#### Scenario: Pair contract preserves binding roles
- **WHEN** a pair rule binds `projectile` and `target`
- **THEN** every handler contract preserves those names, order, and separate required trait sets

#### Scenario: Joined row identities remain distinct bindings
- **WHEN** a rule joins `incoming: Walk` and `outgoing: Walk`
- **THEN** each handler contract records two row roles with the same canonical relation identity and their separate authored names

### Requirement: Pair contracts record binding-qualified reads
A pair handler contract SHALL record each trait read with its relation binding and SHALL also expose the conservative union of canonical read traits for scheduling against existing handlers. Pair selection SHALL determine membership without adding reads.

#### Scenario: Bound reads distinguish roles
- **WHEN** a pair handler reads Collider on both `body` and `wall`
- **THEN** its precise contract contains two bound reads and its conservative read set contains Collider

### Requirement: where: reads are folded into handler contracts
Every trait read reachable from a handler's `where:` predicates SHALL be folded into that handler's contract with the same precision as reads reachable from the handler body: binding-qualified (`bound_reads`) for a pair handler, canonical for a unary handler, in both cases also contributing to the contract's conservative `reads` union and its per-field read access. This SHALL hold whether or not the rule declares `limit:`, even though a limited rule evaluates `where:` as a filter before any limit slot is taken rather than as part of the handler body. Scheduling SHALL see identical data dependencies whether a condition is expressed in `where:` or as a leading `if` in the handler body.

#### Scenario: where: read is recorded like a body read
- **WHEN** a unary handler's `where:` predicate reads `ball.velocity`
- **THEN** the handler's contract records the canonical Ball trait in `reads`, identically to a body read of the same field

#### Scenario: where: bound read distinguishes pair binding roles
- **WHEN** a pair handler's `where:` predicate reads `Collider` through both the `body` and `wall` bindings
- **THEN** its contract's `bound_reads` records two binding-qualified reads and its conservative `reads` set contains Collider once

#### Scenario: where: read on a limited unary rule is recorded
- **WHEN** a unary rule declares `each: Health as h`, `where: h.hp < 20`, and `limit: 3`
- **THEN** each of its handlers' contracts records a read of `Health.hp`, identically to the same rule without `limit:`

#### Scenario: where: bound read on a limited pair rule is recorded
- **WHEN** a pair rule with bindings `actor` and `surface` declares `where: surface[Solid].top <= actor[Actor].feet_y` and `limit: 1 per actor`
- **THEN** each of its handlers' contracts records binding-qualified reads of `Solid` through `surface` and `Actor` through `actor`, and reads of `Solid.top` and `Actor.feet_y`

#### Scenario: A writer orders against a limited where: reader in either declaration order
- **WHEN** one rule's handler writes `Health.hp`, and another rule's handler under `limit:` reads `Health.hp` only in its `where:` predicate
- **THEN** a contract conflict edge on `Health.hp` connects the two handlers, whichever rule is declared first

### Requirement: limit: count reads are folded into handler contracts
Every trait read reachable from a rule's `limit:` count expression SHALL be folded into each of the rule's handler contracts with the same precision as reads reachable from the handler body: binding-qualified (`bound_reads`) for the `per` binding of a pair handler, and also contributing to the contract's conservative `reads` union and per-field read access. A count that reads only constants SHALL add no trait read.

#### Scenario: Per-binding limit count read is recorded
- **WHEN** a pair rule declares `limit: actor[Actor].slots per actor`
- **THEN** each of its handlers' contracts records a binding-qualified read of `Actor` through `actor` and a read of `Actor.slots`

#### Scenario: Constant limit count adds no read
- **WHEN** a rule declares `limit: 3` and nothing else in the rule reads `Actor`
- **THEN** its handler contracts record no read of `Actor`

### Requirement: order by: reads are folded into handler contracts
Every trait read reachable from a handler's `order by:` sort key expressions SHALL be folded into that handler's contract with the same precision as reads reachable from the handler body: binding-qualified (`bound_reads`) for a pair handler, canonical for a unary handler, in both cases also contributing to the contract's conservative `reads` union. Scheduling SHALL see identical data dependencies whether a read occurs in an `order by:` key, a `where:` predicate, or the handler body.

#### Scenario: order by: read is recorded like a body read
- **WHEN** a unary handler's `order by:` sort key reads `ball.velocity`
- **THEN** the handler's contract records the canonical Ball trait in `reads`, identically to a body read of the same field

#### Scenario: order by: bound read distinguishes pair binding roles
- **WHEN** a pair handler's `order by:` sort key reads `Position` through both the `actor` and `target` bindings
- **THEN** its contract's `bound_reads` records two binding-qualified reads and its conservative `reads` set contains Position once

### Requirement: Regular handler contracts are inferred
The semantic analyzer SHALL infer a regular handler's contract from all reachable statements and expressions. Immutable trait field access SHALL add `reads`; mutation SHALL add `writes`; `writes` SHALL mean read/write access; event emission SHALL add `emits`; structural statements SHALL add the corresponding `commands`; and calls to effectful extern functions SHALL add their known effect domain or conservative `external` effect.

#### Scenario: Mutation infers read-write access
- **WHEN** a regular handler assigns through filter alias `transform`
- **THEN** its contract contains the canonical Transform trait in `writes`

#### Scenario: Event and command use are inferred
- **WHEN** a handler emits Contact and spawns Particle
- **THEN** its contract contains Contact in `emits` and `spawn Particle` in `commands`

### Requirement: Projected outputs are distinct contract capabilities
Handler contracts SHALL record projected trait outputs separately from durable writes. Scheduling SHALL treat projection as production of that trait for ordered later matching and reads, while pair read-only validation SHALL continue to prohibit durable writes through pair bindings not admitted by the pair-relations provably-one-per-binding `limit:` carve-out.

#### Scenario: Project output creates producer conflict information
- **WHEN** one handler projects Contacting and a later handler filters and reads Contacting
- **THEN** contract graph construction can order the projection producer before the consumer without classifying projection as a durable component mutation

### Requirement: A write through a provably-limited pair binding is inferred like any other write
When a pair handler mutates a trait through the `per` binding of a provably-one `limit:` clause, contract inference SHALL record that mutation as an ordinary write: folded into the contract's `writes` and contributing to its conservative `reads`/`writes` union for scheduling, identically to a write inferred through a unary filter alias.

#### Scenario: Provably-limited pair write is recorded
- **WHEN** a pair handler under `limit: 1 per actor` assigns `actor[KinematicActor].grounded = true`
- **THEN** the handler's contract records the canonical KinematicActor trait in `writes`

#### Scenario: Scheduling sees the same conflict information as a unary write
- **WHEN** one rule writes a trait through a provably-limited pair binding and another rule reads the same trait through an ordinary `each:` alias
- **THEN** the execution graph orders them using the same read/write conflict rule it would apply between two unary handlers

### Requirement: External handlers declare complete contracts
An `extern rule` handler SHALL declare zero or more `reads:`, `writes:`, `projects:`, `emits:`, `commands:`, and `effects:` blocks because its implementation is unavailable for inference. Trait entries in `reads`, `writes`, and `projects` SHALL resolve through that rule's filter aliases or canonical trait references. Event and command entries SHALL resolve canonically. Projected outputs SHALL remain distinct from durable writes, and the same canonical trait SHALL NOT appear in both `writes` and `projects` for one handler.

#### Scenario: External movement contract resolves aliases
- **WHEN** NativeMovement filters `Motion as motion` and `Transform as transform`, reads motion, and writes transform
- **THEN** its handler contract stores canonical Motion in reads and canonical Transform in writes

#### Scenario: External projection contract resolves trait
- **WHEN** ExternalLayout lists `ComputedLayout` under projects
- **THEN** its handler contract stores canonical ComputedLayout in projected outputs rather than durable writes

#### Scenario: Unknown contract alias is rejected
- **WHEN** an external handler lists `velocity` under reads but no filter binding or trait reference resolves it
- **THEN** semantic analysis reports the unknown contract entry

### Requirement: Contracts constrain external implementations
Generated external-handler APIs SHALL expose immutable access for `reads`, mutable durable access for `writes`, frame-local projection capability only for `projects`, event emission only for `emits`, structural operations only for `commands`, and effect services only for declared `effects`. Backends MUST NOT grant undeclared world mutation or projection through the generated contract API.

#### Scenario: Read entry is immutable
- **WHEN** an external handler lists Sprite only under reads
- **THEN** its generated C++ callback receives const Sprite access

#### Scenario: Write entry is mutable
- **WHEN** an external handler lists Transform under writes
- **THEN** its generated C++ callback receives mutable Transform access

#### Scenario: Project entry exposes only frame-local output
- **WHEN** an external handler lists Highlight under projects but not writes
- **THEN** its generated callback can project Highlight to a live target but cannot durably mutate or attach Highlight through that capability

### Requirement: external projected outputs participate in producer scheduling
An external `projects:` entry SHALL create the same project-producer scheduling information as an inferred regular `project` statement. For the same activation, a projected producer SHALL be ordered before handlers that match or read the projected trait according to the ordinary handler-conflict rules.

#### Scenario: External projection precedes reader
- **WHEN** an external input handler projects PointerHit and a later input handler reads PointerHit
- **THEN** the execution graph orders the producer before the reader without classifying PointerHit as a durable write

### Requirement: Selection does not imply trait access
`each:` and `without:` SHALL determine entity selection without adding reads or writes. An `order by:` expression SHALL add a read for every trait whose data it evaluates.

#### Scenario: Filter-only trait is not read
- **WHEN** a handler filters Disabled as a marker and never accesses its fields
- **THEN** Disabled is absent from the handler's reads and writes

#### Scenario: Sort key adds read
- **WHEN** a rule orders by `transform.position.x`
- **THEN** each affected handler contract includes Transform in reads

### Requirement: Selectionless handler cardinality
A handler with neither positive filter entries nor exclude entries SHALL execute once per phase activation or delivered event occurrence. A handler with any filter or exclude selection clause SHALL execute once per selected entity.

#### Scenario: Filterless input producer runs once
- **WHEN** InputSource has a selectionless `on input` handler
- **THEN** it runs once for the input activation regardless of entity count

#### Scenario: Exclude-only cleanup remains an entity pass
- **WHEN** a handler has only `without: Persistent`
- **THEN** it executes for every non-Persistent selected entity rather than once globally

### Requirement: Command vocabulary and validation
Handler commands SHALL use the forms `spawn Template`, `destroy`, `add Trait`, `remove Trait`, and `set Trait`. Regular inference and external declarations SHALL store canonical template/trait identities, and an external implementation MUST NOT issue a command absent from its handler contract. A `set Trait` command SHALL be recorded only in `commands`; it SHALL NOT add the trait to the handler's `reads` or `writes`, because the patch applies after the activation commit. An external handler that declares `set Trait` SHALL receive a patch capability for that trait only, and it SHALL NOT receive read or write access to that trait through it.

#### Scenario: Contracted structural changes validate
- **WHEN** an external handler declares `spawn Particle`, `destroy`, `add Disabled`, `remove Active`, and `set Health`
- **THEN** all names resolve and the five operations are available to that callback

#### Scenario: Set is inferred as a command, not a write
- **WHEN** a regular handler contains `set Health on target:`
- **THEN** its contract lists `set Health` in `commands` and does not list `Health` in `writes`

#### Scenario: Extern set capability is limited to the declared trait
- **WHEN** an external handler declares `set Health` under `commands:` and no `reads:` or `writes:` for `Health`
- **THEN** its generated callback can queue a `Health` patch on a target entity and cannot read or directly mutate `Health`

### Requirement: Contracts record trait access per field
Every handler contract SHALL record, for each trait in its reads and writes, which fields of that trait are accessed, or that all fields are accessed. Precision SHALL stop at the trait's own fields: an access to a nested member of a field (`transform.position.x`) SHALL count as an access to that field (`position`). A write to a field SHALL also count as a read of that field, matching the existing rule that contract writes are read/write capabilities. The trait-level reads, writes, and projected outputs SHALL keep their existing meaning, and each trait in them SHALL have a field-level entry.

#### Scenario: Field read through a filter alias
- **WHEN** a unary handler filters `Match as m` and reads `m.over`
- **THEN** its contract records a read of `Match.over` and no other `Match` field

#### Scenario: Nested member access stops at the trait field
- **WHEN** a handler assigns `transform.position.x = 1.0` through a `WorldTransform as transform` alias
- **THEN** its contract records a write, and a read, of `WorldTransform.position` only

#### Scenario: Bare field name resolves to its field
- **WHEN** a handler filters `Player` and reads the bare identifier `game_over`, which resolves to `Player.game_over`
- **THEN** its contract records a read of `Player.game_over`

#### Scenario: Pair bound read is recorded per field
- **WHEN** a pair handler reads `body[Collider].radius`
- **THEN** its conservative read set records `Collider.radius`, and its binding-qualified read still names the `body` binding and `Collider`

### Requirement: Unnarrowable accesses cover all fields
A contract SHALL record access to all fields of a trait when the access cannot be narrowed to named fields. This SHALL include: assigning a trait alias itself; a trait-match arm on that trait; an `extern rule` `reads:` or `writes:` entry; a projected output; and any field access whose field the compiler cannot resolve. Passing a trait to a `func` trait parameter SHALL NOT count as an unnarrowable access; it adds the callee's derived field reads (see "Trait arguments add the callee's field reads"). Structural commands (`spawn`, `destroy`, `add`, `remove`, `set`) SHALL continue to add no reads or writes.

#### Scenario: Whole-alias use reads all fields
- **WHEN** a handler passes its `Match as m` alias to a `func` trait parameter, and the `func` reads every field of `Match`
- **THEN** its contract records a read of all fields of `Match`, derived from the `func`, not assumed

#### Scenario: Extern declaration covers all fields
- **WHEN** an `extern rule` handler lists `Match` under `writes:`
- **THEN** its contract records a write, and a read, of all fields of `Match`

#### Scenario: Trait-match arm covers all fields
- **WHEN** a handler contains `match target:` with an arm `Health as h =>`
- **THEN** its contract records a read of all fields of `Health`

#### Scenario: Projection covers all fields
- **WHEN** a handler projects `Highlight`
- **THEN** its contract records `Highlight` as a projected output covering all fields

#### Scenario: Set remains a command without field access
- **WHEN** a handler contains `set Health on target:` assigning only `hp`
- **THEN** its contract lists `set Health` in `commands` and records no read or write of any `Health` field

### Requirement: Contracts record named-entity access
A handler contract SHALL record every named field read and write reachable from the handler body
and from its rule's clauses (`when:`, `where:`, `order by:`, `limit:`). Each access SHALL name the
canonical entity and trait and follow the per-field rules of "Contracts record trait access per
field". Each access SHALL also add the trait and field to the contract's ordinary `reads`/`writes`,
so scheduling orders it against every other handler that touches that trait field, whatever entity
that handler touches. The contract SHALL record the set of `(entity, trait)` pairs that form the
handler's implicit requirement. A contract with at least one named write SHALL be marked as not
splittable per entity. Using an entity name only as a value (`set T on Name`, `emit E to Name`,
comparisons) SHALL add no named access and no requirement.

#### Scenario: Named read is recorded with entity and field
- **WHEN** a handler's rule declares `when: not Game[Match].over`
- **THEN** the contract records a named read of `Game`'s `Match.over`, a read of `Match.over`, and the requirement `(Game, Match)`

#### Scenario: Named write orders against filter readers
- **WHEN** handler A writes `Game[Match].over`, and handler B filters `Match as m` and reads `m.over`
- **THEN** a contract conflict edge on `Match.over` connects A and B

#### Scenario: Disjoint named fields do not conflict
- **WHEN** handler A writes `Game[Match].score`, and handler B only reads `Game[Match].over`
- **THEN** no contract conflict edge connects A and B

#### Scenario: Named write marks the handler unsplittable
- **WHEN** a filter handler runs `Game[Match].score += 1`
- **THEN** its contract is marked as not splittable per entity

#### Scenario: Name as a value adds no access
- **WHEN** a handler runs `set text.ScreenLabel on CrosshairHud:`
- **THEN** its contract lists the `set` command and records no named access and no requirement

### Requirement: Field writes carry a write kind
Every field write recorded in a handler contract SHALL carry one write kind, derived from the writing statements. Authors SHALL NOT declare write kinds. For one handler and one trait field, the analyzer SHALL classify each writing statement as:

- **Accum**: `x += e` or `x -= e` on an `int`, `float`, `duration`, `vec2` or `vec3` field, or on a numeric member of such a field.
- **Join** with an operator: `x = max(x, e)` or `x = max(e, x)` (operator `max`); the same with `min` (operator `min`); `x = x or e`, `x = e or x` or `x = true` (operator `or`); `x = x and e`, `x = e and x` or `x = false` (operator `and`). `max` and `min` SHALL resolve to the `std.math` functions; a user function with the same name SHALL NOT make a Join.
- **Assign**: `x = e` that is not a Join.
- **General**: any other write, including `*=`, `/=`, `%=`, a write through a whole-trait alias, and every `extern rule` `writes:` entry.

A statement is Accum or Join only when `e` does not read `x`. The field's kind for the handler SHALL be the shared kind of all its writing statements when they agree (for Join, also the same operator) and the handler reads `x` nowhere else, including `where:`, `order by:`, `limit:` and `when:`. In every other case the field's kind SHALL be General, except that a field written only by Assign statements and read nowhere else SHALL be Assign. A write that covers all fields of a trait SHALL be General for every field.

#### Scenario: Compound addition is Accum
- **WHEN** a handler filters `Health as h` and runs `h.hp -= 5` and nothing else in it reads `h.hp`
- **THEN** its contract records a write of `Health.hp` with kind Accum

#### Scenario: A countdown is Accum
- **WHEN** a handler filters `Cooldown as c`, runs `c.left -= step.delta` on a `duration` field and reads `c.left` nowhere else
- **THEN** its contract records a write of `Cooldown.left` with kind Accum

#### Scenario: Running maximum is a Join
- **WHEN** a handler runs `stats.best = math.max(stats.best, score)` and `score` is a local
- **THEN** its contract records a write of `Stats.best` with kind Join and operator `max`

#### Scenario: Constant boolean assignment is a Join
- **WHEN** a handler runs `actor.grounded = true` and reads `grounded` nowhere
- **THEN** its contract records a write of `Actor.grounded` with kind Join and operator `or`

#### Scenario: Reading the field elsewhere makes the write General
- **WHEN** a handler runs `h.hp -= 5` and later `if h.hp < 0:`
- **THEN** its contract records a write of `Health.hp` with kind General

#### Scenario: Mixed kinds in one handler are General
- **WHEN** a handler runs `t.x += 1.0` on one path and `t.x = 0.0` on another
- **THEN** its contract records a write of `T.x` with kind General

#### Scenario: Scaling is General
- **WHEN** a handler runs `v.speed *= 0.5`
- **THEN** its contract records a write of `V.speed` with kind General

#### Scenario: Extern writes are General
- **WHEN** an `extern rule` handler lists `Transform` under `writes:`
- **THEN** its contract records a General write of every `Transform` field

#### Scenario: Shadowing max is not a Join
- **WHEN** a module defines its own `func max` and a handler runs `s.best = max(s.best, v)` calling it
- **THEN** its contract records a write of `S.best` with kind General

### Requirement: Set commands record their patched fields
A `set Trait` command inferred from a regular handler SHALL record which fields of the trait its patch assigns. A `set Trait` entry in an `extern rule` `commands:` block SHALL cover all fields. Recording patched fields SHALL NOT add reads or writes to the contract.

#### Scenario: Regular set records assigned fields
- **WHEN** a handler runs `set Health on target:` assigning only `hp`
- **THEN** its contract lists `set Health` with patched field `hp`, and records no read or write of `Health`

#### Scenario: Extern set covers all fields
- **WHEN** an `extern rule` handler declares `set Health` under `commands:`
- **THEN** its contract lists `set Health` covering all fields

### Requirement: Effect calls observe or act
Every effect domain use recorded in a handler contract SHALL be either an observation or an action, derived from the callee. Authors SHALL NOT declare it. A call to a standard-library extern function SHALL observe the domains of its effect summary, except that a call to a function that changes the domain's state SHALL act on them; these are `std.input.consume`, `std.input.set_cursor_captured`, and the `std.editor` functions `spawn_template`, `camera_enter`, `camera_exit`, `apply_camera_2d` and `apply_camera_3d`. A call to an extern function with no known effect summary SHALL act on `external`. Every `effects:` entry of an `extern rule` SHALL act. A domain the handler both observes and acts on SHALL count as acted on.

#### Scenario: Input reads observe
- **WHEN** a handler calls `input.pressed(Jump)` and no other `std.input` function
- **THEN** its contract carries the `input` domain as an observation

#### Scenario: Consume acts
- **WHEN** a handler calls `input.consume(Fire)`
- **THEN** its contract carries the `input` domain as an action

#### Scenario: Unknown extern effect acts
- **WHEN** a handler calls an extern function with no known effect summary
- **THEN** its contract carries the `external` domain as an action

#### Scenario: Extern rule effects act
- **WHEN** an `extern rule` handler lists `graphics` under `effects:`
- **THEN** its contract carries the `graphics` domain as an action

### Requirement: Native stdlib reads are part of the caller's contract
Every stdlib extern function whose native implementation reads trait fields SHALL have a field-read summary: the canonical traits and fields it may read. A call to such a function, from a handler body or from a rule clause the handler's contract folds in, SHALL add every field of its summary to the handler's contract as a read, with the same precision as a body read of that field. Scheduling and exact accumulation SHALL treat these reads like any other read. Authors SHALL NOT declare these reads.

The repository SHALL contain an automated test that enumerates the stdlib extern functions that observe the `world` effect domain and fails when one has no field-read summary. A function that reads no trait field SHALL have an empty summary, stated explicitly.

#### Scenario: A native read is recorded like a body read
- **WHEN** a handler calls `transform.world_position(Mover)` from `std.transform.flat`
- **THEN** its contract records a read of `WorldTransform.position` of `std.transform.flat`

#### Scenario: The writer runs before a native reader
- **WHEN** one `tick` rule writes `t.position.x` through a `WorldTransform as t` alias, and another reads `transform.world_position(Mover).x`, with no explicit order
- **THEN** the graph orders the writer first as writer before reader, and the reader sees the written value whichever rule is declared first

#### Scenario: A two-way native conflict needs an explicit order
- **WHEN** handler A writes `WorldTransform.position` and reads `Seen.x`, handler B writes `Seen.x` from `transform.world_position(Mover).x`, and nothing orders them
- **THEN** compilation fails with an unorientable-conflict error naming both handlers

#### Scenario: An unsummarized world native fails the guard test
- **WHEN** a contributor adds a stdlib extern function in `std.query` and gives it no field-read summary
- **THEN** the guard test fails and names that function

### Requirement: Contracts record relation access
A handler contract SHALL record a read of relation `R` for every row binding of `R` and every `R(x, y)` test in its rule's `where:` or `when:`, and the fields of `R` that the handler reads. `add R` and `remove R` SHALL be recorded as commands only, like `set Trait`: they apply at the commit and add no read or write of `R`. Relation commands SHALL follow the same ordering and race rules as trait `add` and `remove` commands. A kept relation's rule SHALL be its only writer. The end traits of `R` SHALL NOT count as reads of those traits.

#### Scenario: Test is a relation read
- **WHEN** a rule declares `where: not Claim(robot, _)`
- **THEN** its handler's contract records a read of `Claim`

#### Scenario: Add is a command
- **WHEN** a handler writes `add Claim(robot, task)`
- **THEN** its contract lists `add Claim` in `commands` and no read or write of `Claim`

#### Scenario: Row field read
- **WHEN** a handler with row binding `r: Walk` reads `r.cost`
- **THEN** its contract records a read of `Walk.cost` and no other `Walk` field

#### Scenario: Keep run before its readers
- **WHEN** `WalkLinks` keeps `Walk` and another rule in the same phase tests `Walk(a, b)`
- **THEN** the scheduler orders them the same way it orders a kept trait's keeper and readers

### Requirement: Trait arguments add the callee's field reads
For each trait parameter of a user `func`, the compiler SHALL derive the set of that trait's fields the `func` may read: fields read directly in its body, plus the fields read by every `func` it passes that parameter to, transitively. A call that passes a trait argument, from a handler body or from any rule clause the handler's contract folds in, SHALL add exactly that set to the handler's contract as reads of the argument's trait, with the same precision as inline reads at the call site. For a pair handler the reads SHALL be qualified by the argument's binding; for a named read they SHALL be recorded as named-entity accesses. Scheduling, exact accumulation and keep-loop detection SHALL treat these reads like any other read. Event arguments SHALL add no trait read.

#### Scenario: Only read fields are recorded
- **WHEN** a unary handler with `each: tv.WorldTransform as t` calls `height(t)`, and `height` reads only `t.position.y`
- **THEN** its contract records a read of `WorldTransform.position` and no other `WorldTransform` field

#### Scenario: Reads through a passed-on parameter
- **WHEN** `to_screen(camera: CameraView, …)` reads `camera.forward` and passes `camera` to `has_area`, which reads `camera.rect_size`, and a handler calls `to_screen(cv, p)`
- **THEN** the handler's contract records reads of `CameraView.forward` and `CameraView.rect_size`

#### Scenario: Pair argument is binding-qualified
- **WHEN** a pair handler passes `camera[CameraView]` to a `func` that reads `position`
- **THEN** its binding-qualified reads name the `camera` binding and `CameraView`, and its per-field reads record `CameraView.position`

#### Scenario: A writer of an unread field is not ordered before the caller
- **WHEN** rule A writes `WorldTransform.scale`, rule B in the same phase calls `height(t)`, which reads only `position`, and nothing orders them
- **THEN** the graph adds no edge between A and B for `WorldTransform`

### Requirement: Derived relation reads are source reads
A handler contract that reads a derived relation SHALL record a read of each relation it is derived from, and of the cost field for `shortest`. Ordering and race checks SHALL treat it as reading those source relations.

#### Scenario: Closure read
- **WHEN** a rule tests `Reach(heart, t)` where `Reach = closure(Walk)`
- **THEN** its contract records a read of `Walk`

#### Scenario: Shortest read
- **WHEN** a rule binds `r: Route` where `Route = shortest(Walk, by = cost)`
- **THEN** its contract records a read of `Walk` and of `Walk.cost`

### Requirement: Extern contracts can read relations
An `extern rule` handler's `reads:` block SHALL accept relation names alongside trait names. A relation entry SHALL resolve canonically, like a trait entry. It SHALL record a read of the whole relation and give the native implementation read access to that relation's rows only. An extern `writes:`, `projects:` or `commands:` entry naming a relation SHALL be rejected in this change.

#### Scenario: Renderer reads HiddenIn
- **WHEN** a stdlib renderer's extern rule lists `visibility.HiddenIn` under `reads:`
- **THEN** its contract records a read of `HiddenIn`, and keep rules for `HiddenIn` order against it as they would for a kept trait read

#### Scenario: Extern write to a relation
- **WHEN** an extern rule lists a relation under `writes:`
- **THEN** the compiler reports that extern rules may only read relations

### Requirement: Joined row domains read both relations and their sources

A handler over two row bindings SHALL record reads of both relations for membership and row-field selection, even if its body uses only one binding. A derived binding SHALL contribute all transitive source-relation reads and shortest-path cost reads required by the existing derived-read contract. Other expressions in `where:`, `when:`, `order by:` and the body SHALL contribute their normal reads. Scheduling, occurrence conflict checks and keep-dependency analysis SHALL use these reads without converting a row role into an entity role or granting it write access.

#### Scenario: An unused row binding still affects membership
- **WHEN** a join's body emits only fields of `a`, while `b` determines which tuples match
- **THEN** the contract still records a read of `b`'s relation

#### Scenario: A derived binding contributes its source cost
- **WHEN** a rule joins `Located` to `Route = shortest(Walk, by = cost)`
- **THEN** its contract reads `Located`, the derived route relation, `Walk`, and `Walk.cost` through the existing canonical derived-read representation

#### Scenario: Joined handlers retain occurrence conflict checks
- **WHEN** two otherwise unordered producers have descendant runs where a joined handler removes relation rows and another handler adds rows of that relation at a conflicting occurrence position
- **THEN** the existing occurrence conflict analysis diagnoses or orders the normal structural commands, and the joined handler still records reads of both of its row domains

#### Scenario: Derived keep dependencies remain visible
- **WHEN** a source relation is kept and a joined handler reads a relation derived from it
- **THEN** dependency analysis records that source read, and the join sees the source rows committed before its pass rather than uncommitted keep commands

### Requirement: Projection orders every membership observer
A handler observes the membership of a trait when its result can depend on which entities carry that trait. A handler SHALL observe membership of every trait named by its `each:` or `without:` clauses, by a pair binding, or by the filter of a native query call in its body, whether that filter entry is positive or negated. Observing membership SHALL NOT add a trait read, as "Selection does not imply trait access" requires.

For one activation, a handler that projects a trait SHALL be ordered before every other handler that observes the membership of that trait. An external `projects:` entry SHALL count as a projection. Two handlers that each observe the membership of a trait the other projects SHALL be an unorientable conflict, reported as a compile error naming both handlers, unless the existing rule for conflicts with an imported `pub group` orders them. A handler SHALL still be ordered as an observer when it also projects the same trait.

#### Scenario: Excluding filter runs after the projector
- **WHEN** one rule runs `project A` and another rule has `without: A`, with no explicit order, in the same phase
- **THEN** the projector runs first, and the result is the same in either declaration order

#### Scenario: Native query runs after the projector
- **WHEN** one rule runs `project A` and another rule calls `query.exists[A]()`, with no explicit order, in the same phase
- **THEN** the projector runs first, and the result is the same in either declaration order

#### Scenario: Pair binding runs after the projector
- **WHEN** one rule runs `project A` and a pair rule has a binding that requires `A`, with no explicit order, in the same phase
- **THEN** the projector runs first

#### Scenario: Observing a projector that also projects
- **WHEN** one rule runs `project A` and another rule has `each: A` and also runs `project A`
- **THEN** the first rule runs before the second

#### Scenario: Mutual negation is rejected
- **WHEN** one rule has `without: B` and runs `project A`, and another has `without: A` and runs `project B`, in the same phase with no explicit order
- **THEN** the compiler reports an unorientable conflict naming both handlers

#### Scenario: An explicit order that disagrees is a cycle
- **WHEN** the rule that projects `A` declares `after:` a rule with `without: A`
- **THEN** the compiler reports a schedule cycle naming both handlers

#### Scenario: An explicit order cannot settle mutual observation
- **WHEN** two rules each observe what the other projects, and one declares `after:` the other
- **THEN** the compiler reports a schedule cycle naming both handlers

#### Scenario: An observer that feeds its projector needs an explicit order
- **WHEN** a rule observes `A` and writes a field that the rule projecting `A` reads, with no explicit order
- **THEN** the compiler reports an unorientable conflict that suggests only running the observer after the projector

#### Scenario: A public group member projects what an importer observes
- **WHEN** an importing rule observes a trait projected by a member of an imported `pub group`, and also writes what another member reads
- **THEN** the projecting member runs before the importing rule, and the importing rule runs before the other member

#### Scenario: The importer rule cannot run an observer ahead of its projector
- **WHEN** an importing rule observes a trait projected by a member of an imported `pub group`, and also writes what that member reads
- **THEN** the compiler reports a schedule cycle naming both handlers

#### Scenario: Commuting writes do not remove the order
- **WHEN** a projector and an observer of the same trait only accumulate into the same field of that trait
- **THEN** the projector still runs first

#### Scenario: Two external producers of one trait are rejected
- **WHEN** two external handlers in one activation each select and project the same trait
- **THEN** the compiler reports an unorientable conflict naming both handlers

#### Scenario: Observation adds no read
- **WHEN** a handler has `without: A` and never accesses a field of `A`
- **THEN** `A` is absent from the handler's reads

### Requirement: A pass that observes its own projection fixes its rows
A unary pass that projects a trait whose membership it observes SHALL choose its rows at pass start and run them in creation order. A projection made by one row SHALL NOT change which rows of that pass run.

#### Scenario: A projection does not drop a later row
- **WHEN** a pass has `without: A`, two entities match at pass start, and the first entity's row runs `project A to` the second entity
- **THEN** both rows run, in creation order, and the result does not depend on storage order

#### Scenario: A pass with no `each:` keeps its rows
- **WHEN** a pass has only a `without: A` filter, and a row runs `project A to` an entity whose row has not run yet
- **THEN** that entity's row still runs

### Requirement: Reads through a link are recorded as target trait reads
A read `x.f[T].g` through a required link (`dsl-required-links`) SHALL add to the handler's contract a read of the link field `H.f` and a read of `T.g` on any entity, not only the row's own entity. It SHALL order the handler against writers of `T.g` like any other read of `T.g`, and against commands that can lose the link like a read of membership in `T`. Reads through a link in `where:`, `when:`, `order by:`, reducer inputs and keep fields SHALL be folded into the contract like other reads of those clauses.

#### Scenario: Writer before a reader through a link
- **WHEN** one `tick` handler writes `Unit.health` and another, with no explicit order, reads `order.target[Unit].health`
- **THEN** the writer runs first

#### Scenario: Keep through a link depends on the target trait
- **WHEN** a keep rule's `where:` reads `follow.target[tf.WorldTransform].position`
- **THEN** its contract records a read of `WorldTransform.position`, and the keep dependency analysis counts it

### Requirement: Contracts are audited against real accesses
The repository SHALL provide an audit build mode for generated programs. In that mode, while a handler runs, every field read and write it performs in generated code, every event it emits, every structural command it issues and every effect it uses SHALL be checked against its contract, including write kinds; and every trait a native function accesses during a call from the handler SHALL be checked against that native's field-read summary. Every other component access during the handler SHALL be checked at trait level against the traits its contract selects, reads, writes, projects or commands. An access outside the contract or summary SHALL fail the test and report the handler's canonical identity, the access and the source location of the statement or native call, when the access has one. Audit mode SHALL NOT change the program's results, and normal builds SHALL contain no audit code.

#### Scenario: A missing native read fails
- **WHEN** a stdlib native reads `WorldTransform` during a call and its field-read summary lists no field of `WorldTransform`
- **THEN** the audited test fails and names the native, the calling handler and `WorldTransform`

#### Scenario: A write of another kind fails
- **WHEN** a handler's contract records an Accum write of `Health.hp` and the handler's code performs an Assign write of it
- **THEN** the audited test fails and names the handler, `Health.hp` and both kinds

#### Scenario: Selection is not a read
- **WHEN** a handler with `each: Unit` never reads a `Unit` field
- **THEN** the audit reports nothing for `Unit`

### Requirement: Every summarized native runs under audit
The audited test set SHALL together call every stdlib extern function that has a field-read summary. A test SHALL fail when such a function was never called under audit, naming the function.

#### Scenario: A new native without an audited call
- **WHEN** a stdlib native gains a field-read summary and no audited fixture calls it
- **THEN** the native coverage test fails and names the function
