# dsl-pair-relations Specification

## Purpose
Define the DSL surface, semantics, and validation rules for binary pair-relation domains in regular rules: the `pairs:` clause, pair bindings as entity identifiers and trait namespaces, directed and finite pair relation semantics, deterministic membership snapshots, read-only durable trait access through pair bindings, explicit-target-only handler behavior, and the one-node execution-graph shape of a pair handler.

## Requirements

### Requirement: Regular rules support a binary pair domain
A regular `rule` SHALL accept a `pairs:` block containing exactly two ordered, uniquely named entity bindings. Each binding SHALL contain one or more positive trait entries using the same dotted trait-name and optional `as` alias form as unary filters. `pairs:` SHALL be mutually exclusive with `filter:`, `exclude:`, and `order by:` and SHALL NOT be accepted on `extern rule` declarations.

#### Scenario: Two-binding pair rule is accepted
- **WHEN** `DetectContacts` declares `pairs:` bindings `body` and `wall`, each with at least one declared trait
- **THEN** the rule has a binary pair domain in source binding order

#### Scenario: Pair and unary domains cannot be mixed
- **WHEN** one rule declares both `pairs:` and `filter:`
- **THEN** semantic analysis reports that a rule must choose one execution domain

#### Scenario: Pair domain requires exactly two non-empty bindings
- **WHEN** a `pairs:` block has one or three bindings, or a binding has no trait entries
- **THEN** compilation reports an invalid pair domain

### Requirement: Pair bindings are entity identifiers and trait namespaces
Each pair binding SHALL have type `entity_id` and SHALL also namespace the traits selected for that entity. `binding.Trait.field` SHALL access an unaliased local trait, `binding.module_alias.Trait.field` SHALL preserve imported qualification, and `binding.trait_alias.field` SHALL use an alias declared only within that binding. Pair binding names and their aliases MUST be unambiguous within every handler scope.

#### Scenario: Binding is used as an entity target
- **WHEN** a pair handler executes `emit Contact to body`
- **THEN** `body` type-checks as the current tuple's left `entity_id`

#### Scenario: Qualified imported trait is accessed through a binding
- **WHEN** binding `body` selects `tf.WorldTransform` and the handler reads `body.tf.WorldTransform.position`
- **THEN** the access resolves to the canonical imported WorldTransform trait on `body`

#### Scenario: Binding-local alias shortens access
- **WHEN** binding `body` selects `tf.WorldTransform as transform`
- **THEN** `body.transform.position` resolves to that selected trait

### Requirement: Pair relation semantics are directed and finite
For bindings A and B, a pair handler SHALL execute over the directed Cartesian product of the live entities satisfying A and the live entities satisfying B. Self-pairs and reverse-role tuples SHALL be included when membership permits. Handler `if` statements SHALL retain ordinary imperative semantics and SHALL be the authored mechanism for rejecting tuples. A `return` statement SHALL be an equally valid authored rejection mechanism and SHALL end only the current tuple's invocation — it SHALL NOT abort the remaining tuples in the pass.

#### Scenario: Same relation includes directed tuples
- **WHEN** both bindings select the same two entities `a` and `b`
- **THEN** the relation contains `(a,a)`, `(a,b)`, `(b,a)`, and `(b,b)`

#### Scenario: Imperative condition rejects a tuple
- **WHEN** the handler begins with `if body != wall:`
- **THEN** self-pair bodies do not execute while relation construction remains unchanged

#### Scenario: Early return rejects only the current tuple
- **WHEN** the handler begins with `if a == b: return` and the pass includes self-pairs interleaved with non-self-pairs
- **THEN** each self-pair tuple's invocation ends at the `return` while every other tuple in the pass still executes its full body

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
Pair handlers SHALL permit reads of selected durable traits but SHALL reject direct or indirect mutation of those traits, including assignment, compound assignment, and writable trait-match aliases derived from a pair binding. Selection itself SHALL NOT count as a trait read.

#### Scenario: Pair trait field read is accepted
- **WHEN** a condition reads `body.Collider.mask` and `wall.Collider.layer`
- **THEN** both binding-qualified reads are accepted and inferred

#### Scenario: Pair trait field mutation is rejected
- **WHEN** a pair handler assigns `body.Transform.x += 1.0`
- **THEN** semantic analysis reports that pair-bound durable traits are read-only

#### Scenario: Trait match cannot bypass read-only access
- **WHEN** a pair handler matches directly on `body` to obtain a data-bearing mutable trait alias
- **THEN** semantic analysis rejects the match in the initial pair profile

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
