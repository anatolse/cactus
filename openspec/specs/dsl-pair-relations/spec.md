# dsl-pair-relations Specification

## Purpose
Define the DSL surface, semantics, and validation rules for binary pair-relation domains in regular rules: the `pairs:` clause, pair bindings as entity identifiers and trait namespaces, directed and finite pair relation semantics, deterministic membership snapshots, read-only durable trait access through pair bindings, explicit-target-only handler behavior, and the one-node execution-graph shape of a pair handler.

## Requirements

### Requirement: Regular rules support a binary pair domain
A regular `rule` SHALL accept a `pairs:` block containing exactly two ordered, uniquely named bindings. Each binding SHALL be an entity binding or a row binding. An entity binding SHALL contain one or more positive trait entries using the same dotted trait-name and optional `as` alias form as unary filters. A row binding SHALL contain exactly one entry, a relation name with no alias (`dsl-relations`). At most one binding SHALL be a row binding. `pairs:` SHALL be mutually exclusive with `each:` and `without:`, MAY be combined with `order by:` (`dsl-rule-order-by`), and SHALL NOT be accepted on `extern rule` declarations.

#### Scenario: Two-binding pair rule is accepted
- **WHEN** `DetectContacts` declares `pairs:` bindings `body` and `wall`, each with at least one declared trait
- **THEN** the rule has a binary pair domain in source binding order

#### Scenario: Pair and unary domains cannot be mixed
- **WHEN** one rule declares both `pairs:` and `each:`
- **THEN** semantic analysis reports that a rule must choose one execution domain

#### Scenario: Pair domain requires exactly two non-empty bindings
- **WHEN** a `pairs:` block has one or three bindings, or a binding has no trait entries
- **THEN** compilation reports an invalid pair domain

#### Scenario: Pairs rule may declare order by
- **WHEN** a `pairs:` rule also declares an `order by:` block
- **THEN** semantic analysis accepts the combination, unlike `each:` or `without:` combined with `pairs:`

#### Scenario: Row binding next to an entity binding
- **WHEN** a `pairs:` block declares `w: Walker` and `r: Route`, where `Route` is a relation
- **THEN** `w` is an entity binding and `r` is a row binding

#### Scenario: Relation mixed with traits in one binding
- **WHEN** a binding lists `Route` and `Tile`, where `Route` is a relation
- **THEN** the compiler reports that a row binding has exactly one relation entry

### Requirement: Pair bindings are entity identifiers and trait namespaces
Each entity binding SHALL have type `entity_id` and SHALL select the traits listed under it. A row binding SHALL follow `dsl-relations` instead. `binding[Trait].field` SHALL access an unaliased selected trait, and `binding[module_alias.Trait].field` SHALL keep the imported qualification. The brackets SHALL contain exactly one trait reference, and it SHALL be a trait selected under that binding. A trait selected with `as alias` SHALL be accessed by the bare alias, `alias.field`, with no binding prefix. Aliases SHALL share one scope per rule: an alias name SHALL be unique across all bindings of the rule and SHALL NOT equal a binding name. An alias SHALL be visible wherever its binding is visible, so an alias of a binding that `reduce per` eliminates is not visible in the reduced handler.

#### Scenario: Binding is used as an entity target
- **WHEN** a pair handler executes `emit Contact to body`
- **THEN** `body` type-checks as the current tuple's left `entity_id`

#### Scenario: Unaliased trait is accessed through brackets
- **WHEN** binding `body` selects `Collider` and the handler reads `body[Collider].mask`
- **THEN** the access resolves to the `Collider` trait on `body`

#### Scenario: Qualified imported trait is accessed through a binding
- **WHEN** binding `body` selects `tf.WorldTransform` and the handler reads `body[tf.WorldTransform].position`
- **THEN** the access resolves to the canonical imported WorldTransform trait on `body`

#### Scenario: Binding-local alias shortens access
- **WHEN** binding `body` selects `tf.WorldTransform as transform`
- **THEN** `transform.position` resolves to that selected trait on `body`

#### Scenario: Unselected trait in brackets is rejected
- **WHEN** binding `body` selects only `Collider` and the handler reads `body[Health].hp`
- **THEN** semantic analysis reports that `body` does not select `Health`

#### Scenario: Duplicate alias across bindings is rejected
- **WHEN** binding `body` selects `tv.WorldTransform as transform` and binding `wall` also selects `tv.WorldTransform as transform`
- **THEN** semantic analysis reports that alias `transform` is declared twice in the rule

#### Scenario: Alias equal to a binding name is rejected
- **WHEN** binding `wall` selects `Collider as body`, and the rule also has a binding named `body`
- **THEN** semantic analysis reports that alias `body` clashes with a binding name

#### Scenario: Eliminated binding's alias is not visible after reduce
- **WHEN** a rule declares `reduce per team:` over bindings `team` and `player`, `player` selects `Score as score`, and the handler reads `score.points`
- **THEN** semantic analysis reports that `score` is not available in the reduced handler

### Requirement: Pair relation semantics are directed and finite
For bindings A and B, a pair handler SHALL execute over the directed Cartesian product of the live entities satisfying A and the live entities satisfying B. Self-pairs and reverse-role tuples SHALL be included when membership permits, unless excluded by `where:` or by a handler `if`/`return`. Handler `if` statements SHALL retain ordinary imperative semantics and, together with `where:` predicates, SHALL be the authored mechanisms for rejecting tuples. A `return` statement SHALL be an equally valid authored rejection mechanism and SHALL end only the current tuple's invocation — it SHALL NOT abort the remaining tuples in the pass.

#### Scenario: Same relation includes directed tuples
- **WHEN** both bindings select the same two entities `a` and `b`
- **THEN** the relation contains `(a,a)`, `(a,b)`, `(b,a)`, and `(b,b)`

#### Scenario: Imperative condition rejects a tuple
- **WHEN** the handler begins with `if body != wall:`
- **THEN** self-pair bodies do not execute while relation construction remains unchanged

#### Scenario: Early return rejects only the current tuple
- **WHEN** the handler begins with `if a == b: return` and the pass includes self-pairs interleaved with non-self-pairs
- **THEN** each self-pair tuple's invocation ends at the `return` while every other tuple in the pass still executes its full body

#### Scenario: where: rejects a tuple before the handler body runs
- **WHEN** a pair rule declares `where: a != b`
- **THEN** self-pair tuples are absent from the pass and never begin their handler body invocation

### Requirement: where: composes with the pair snapshot
`where:` predicates on a pair rule SHALL be evaluated per tuple, after the pair membership snapshot is taken and before that tuple's handler body executes, against the bindings' already-snapshotted values. `where:` SHALL only remove tuples from the directed Cartesian product described above; it SHALL NOT add tuples, and it SHALL NOT be re-evaluated mid-pass as a result of mutations, projections, or buffered structural commands performed by earlier tuples in the same pass. Left-binding-major tuple order is unaffected by which tuples `where:` subsequently rejects.

#### Scenario: where: does not reorder the remaining tuples
- **WHEN** a pair rule's `where:` predicate rejects some tuples from a left-binding-major snapshot ordering `(a,x), (a,y), (b,x), (b,y)`
- **THEN** the tuples that remain still execute in that same relative order

#### Scenario: where: cannot expand the pass
- **WHEN** an earlier tuple's invocation performs a mutation that would make a later tuple newly satisfy a `where:` predicate it previously failed
- **THEN** that later tuple's membership for the current pass is unaffected

### Requirement: Pair passes use deterministic membership snapshots
Before executing tuple bodies, the runtime SHALL snapshot both binding memberships in stable entity-creation order and SHALL iterate their product left-binding-major. Membership SHALL remain fixed for the complete handler pass; component values need not be copied. Projected traits and buffered structural commands produced during the pass SHALL NOT add or remove tuples from that pass.

#### Scenario: Left-binding-major order is stable
- **WHEN** left snapshot is `[a,b]` and right snapshot is `[x,y]`
- **THEN** tuple order is `(a,x)`, `(a,y)`, `(b,x)`, `(b,y)`

#### Scenario: Projection does not expand current pass
- **WHEN** an early tuple projects a trait required by one pair binding onto another entity
- **THEN** that entity is not added to the already-snapshotted pass

#### Scenario: Buffered removal does not shrink current pass
- **WHEN** an early tuple queues removal of a required trait from an entity appearing in later tuples
- **THEN** later snapshotted tuples still execute and the removal becomes visible only at activation commit

### Requirement: Pair-bound durable trait access is read-only
Pair handlers SHALL permit reads of selected durable traits. Mutation of a pair-bound durable trait — assignment, compound assignment, or a writable trait-match alias derived from a pair binding — SHALL be rejected, UNLESS that binding is named by a `per` part of a `limit:` clause (`dsl-rule-limit`) whose count expression the compiler proves equal to the literal `1`. In that case, only the named bindings become writable, through `binding[Trait].field` and through the bare aliases of their traits. A binding not named by a provably-one `per` part, any binding under a limit that is not provably one or under a global limit, and every row binding remain read-only. Selection itself SHALL NOT count as a trait read.

#### Scenario: Pair trait field read is accepted
- **WHEN** a condition reads `body[Collider].mask` and `wall[Collider].layer`
- **THEN** both bracketed reads are accepted and inferred

#### Scenario: Pair trait field mutation is rejected
- **WHEN** a pair handler assigns `body[Transform].x += 1.0`
- **THEN** semantic analysis reports that pair-bound durable traits are read-only

#### Scenario: Mutation through a read-only alias is rejected
- **WHEN** binding `body` selects `Transform as pose`, and a pair handler assigns `pose.x += 1.0` with no `limit:` on `body`
- **THEN** semantic analysis reports that pair-bound durable traits are read-only

#### Scenario: Trait match cannot bypass read-only access
- **WHEN** a pair handler matches directly on `body` to obtain a data-bearing mutable trait alias
- **THEN** semantic analysis rejects the match in the initial pair profile

#### Scenario: Mutation through a provably-one-per-binding is accepted
- **WHEN** a rule declares `limit: 1 per actor` and a handler assigns `actor[KinematicActor].ground_surface = top`
- **THEN** semantic analysis accepts the mutation

#### Scenario: Alias write through a provably-one-per-binding is accepted
- **WHEN** a rule declares `limit: 1 per actor`, `actor` selects `KinematicActor as kinematic`, and a handler assigns `kinematic.grounded = true`
- **THEN** semantic analysis accepts the mutation

#### Scenario: The non-limited binding remains read-only under a per-binding limit
- **WHEN** a rule declares `limit: 1 per actor` with bindings `actor` and `surface`, and a handler assigns to `surface`
- **THEN** semantic analysis rejects the mutation, since `surface` is not the limited binding

#### Scenario: Mutation is rejected when the limit is not provably one
- **WHEN** a rule declares `limit: 2 per actor` and a handler assigns to `actor`
- **THEN** semantic analysis rejects the mutation, since `2` does not prove at most one tuple per `actor`

#### Scenario: Both bindings are writable under a two-sided limit of one
- **WHEN** a rule declares `limit: 1 per robot, 1 per task` and a handler assigns to fields of both `robot` and `task`
- **THEN** semantic analysis accepts both mutations

### Requirement: Pair handlers require explicit entity targets
A pair handler SHALL have no implicit current entity. `self` and statement forms that default to `self` SHALL be rejected. Explicit-target event emission, projection, add, remove, destroy, and spawn operations SHALL remain available subject to their ordinary typing, total-handle, buffering, and contract rules. Untargeted event emission SHALL remain a broadcast occurrence.

#### Scenario: Explicit-target projection is accepted
- **WHEN** a pair handler executes `project GroundContact to body`
- **THEN** the projection targets the tuple's `body` entity

#### Scenario: Implicit destroy is rejected
- **WHEN** a pair handler contains bare `destroy`
- **THEN** semantic analysis reports that pair handlers require an explicit target

#### Scenario: Untargeted event remains valid
- **WHEN** a pair handler emits an event without `to`
- **THEN** one broadcast occurrence is emitted for that tuple

### Requirement: One pair handler is one execution-graph node
A pair handler SHALL produce one static handler execution-graph node identified by its rule and trigger. Runtime tuples SHALL be invocations within that node and MUST NOT become graph nodes. The complete tuple pass SHALL finish before the dispatcher advances to another handler node or drains events emitted by the activation.

#### Scenario: Many tuples retain one node
- **WHEN** `DetectContacts.fixed_tick` executes for three body-wall tuples
- **THEN** the execution graph still contains one `DetectContacts.fixed_tick` node

#### Scenario: Emitted events wait for pass completion
- **WHEN** several tuples emit Contact occurrences
- **THEN** the complete pair pass finishes before the activation event cascade delivers those occurrences

### Requirement: Compact pair binding trait lists
Each binding inside `pairs:` SHALL accept `binding: TraitA, TraitB as alias` as an equivalent spelling of its existing indented positive trait list. Inline and block bindings SHALL preserve qualified references, rule-scoped aliases, and source order, and SHALL coexist in the same pair declaration. Each binding SHALL contain at least one trait; the domain SHALL still contain exactly two ordered, uniquely named bindings. An inline binding SHALL NOT have a block continuation.

#### Scenario: Compact aliases retain their binding namespace
- **WHEN** a binding declares `body: Health as health, tf.WorldTransform as pose`
- **THEN** `health.current` and `pose.position` resolve as they do for the equivalent block declaration

#### Scenario: Inline product retains deterministic behavior
- **WHEN** a compact pair domain selects the same entities as its block-form equivalent
- **THEN** both retain directed self/reverse tuples, stable membership snapshots, tuple order, access restrictions, and acceleration eligibility

#### Scenario: Compact cardinality errors remain errors
- **WHEN** a compact `pairs:` declaration has one or three bindings, duplicate binding names, or an empty binding
- **THEN** compilation rejects it under the existing pair-domain rules

#### Scenario: Extern compact pairs remain rejected
- **WHEN** an extern rule declares compact pair bindings
- **THEN** compilation rejects the pair domain and continues reporting later diagnostics

### Requirement: Dotted pair-binding access is rejected with a rename diagnostic
A member path that starts with a pair binding and continues with a trait selected under it, written with dots (`body.Collider.mask`, `body.tf.WorldTransform.position`), SHALL be a compile error whose diagnostic names the bracketed replacement (`body[Collider].mask`). A member path that starts with a pair binding and continues with one of its aliases (`body.pose.position`) SHALL be a compile error whose diagnostic says that aliases are written bare (`pose.position`). Each such access SHALL be reported once, in handler bodies and in every rule clause, and analysis SHALL continue as if the replacement had been written.

#### Scenario: Dotted unaliased access names the bracket form
- **WHEN** a pair handler reads `body.Collider.mask`
- **THEN** compilation reports one error that says to use `body[Collider].mask`

#### Scenario: Dotted imported access names the bracket form
- **WHEN** a pair rule's `where:` reads `body.tf.WorldTransform.position`
- **THEN** compilation reports one error that says to use `body[tf.WorldTransform].position`

#### Scenario: Binding-prefixed alias names the bare form
- **WHEN** binding `body` selects `tf.WorldTransform as pose` and a handler reads `body.pose.position`
- **THEN** compilation reports one error that says aliases are written bare, as `pose.position`

#### Scenario: Alias in brackets is rejected
- **WHEN** binding `body` selects `tf.WorldTransform as pose` and a handler reads `body[pose].position`
- **THEN** compilation reports that `pose` is an alias and says to use `pose.position`
