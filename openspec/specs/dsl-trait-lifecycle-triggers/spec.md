# dsl-trait-lifecycle-triggers Specification

## Purpose
Lets rules react when a durable trait arrives on or leaves an entity, so gameplay state can be
modeled with `add`/`remove` marker traits instead of bool flags checked in every rule.
## Requirements
### Requirement: Lifecycle trigger syntax
A rule handler MAY use `on added T` or `on removed T` as its trigger, where `T` resolves to a
durable trait declaration. An optional `as IDENTIFIER` clause MAY follow the trait name.
`added` and `removed` SHALL be recognized as trigger words only in this position, followed by
a trait name; they SHALL NOT become reserved words, and `on added:` SHALL still name a user
event called `added`. The handler's canonical identity SHALL be composed from its rule, the
trigger kind, and the trait's canonical identity.

#### Scenario: Added trigger accepted
- **WHEN** a rule declares `on added Dying:` and `Dying` is a declared trait
- **THEN** the handler is accepted with an added-trigger on the canonical trait `Dying`

#### Scenario: Removed trigger with alias accepted
- **WHEN** a rule declares `on removed Burning as old:` and `Burning` is a declared trait
- **THEN** the handler is accepted with a removed-trigger on `Burning` and a binding named `old`

#### Scenario: Unknown trait rejected
- **WHEN** a rule declares `on added Nope:` and no trait `Nope` is in scope
- **THEN** the compiler reports an unknown-trait error at the trait name

#### Scenario: User event named added still works
- **WHEN** a module declares `event added` and a rule declares `on added:`
- **THEN** the handler is an ordinary event handler for that event

#### Scenario: Canonical identities distinguish trigger kinds
- **WHEN** one rule declares both `on added Stunned:` and `on removed Stunned:`
- **THEN** the two handlers have distinct canonical identities

### Requirement: Net-change firing per commit round
At each commit round, for every entity and every trait that has at least one lifecycle
trigger in the linked program, the runtime SHALL compare whether the entity carried the trait
durably before the round with whether it carries it after the round. It SHALL deliver
`on added T` when the trait went from absent to present, `on removed T` when it went from
present to absent, and nothing otherwise. Spawning an entity SHALL count as the arrival of
each of its traits. `set` and ordinary field writes SHALL NOT fire lifecycle triggers.

#### Scenario: Add fires added
- **WHEN** a handler issues `add Dying` on an entity without `Dying`, and the round commits
- **THEN** `on added Dying` handlers run for that entity

#### Scenario: Spawn fires added for each watched trait
- **WHEN** a handler spawns an entity whose template includes `Enemy`, and a rule declares `on added Enemy`
- **THEN** that handler runs for the new entity after the round commits

#### Scenario: Add and remove in one round fire nothing
- **WHEN** one round both adds and removes `Stunned` on an entity that did not carry it before
- **THEN** neither `on added Stunned` nor `on removed Stunned` runs for that entity

#### Scenario: Replacing an existing trait fires nothing
- **WHEN** a handler issues `add Stunned:` with new field values on an entity that already carries `Stunned`
- **THEN** no lifecycle trigger for `Stunned` runs

#### Scenario: Set fires nothing
- **WHEN** a handler issues `set Stunned on e:` for an entity carrying `Stunned`
- **THEN** no lifecycle trigger runs

#### Scenario: Remove and add in one round fire nothing
- **WHEN** one round removes and re-adds `Stunned` on an entity that carried it before
- **THEN** no lifecycle trigger for `Stunned` runs

### Requirement: Targeted delivery after commit
A lifecycle trigger SHALL be delivered only to the entity whose trait set changed, after the
commit round that changed it, and SHALL observe the committed state. The handler SHALL run for
that entity only if the entity matches the rule's `filter:`, `exclude:` and `when:` in that
committed state. `on added T` SHALL imply that the entity carries `T`; `on removed T` SHALL
imply that it does not. Authors SHALL NOT need to list `T` in `filter:` or `exclude:`.

#### Scenario: Other matching entities are not triggered
- **WHEN** two entities carry `Enemy` and only one of them gains `Dying`
- **THEN** a rule with `filter: Enemy` and `on added Dying` runs exactly once, for the entity that gained `Dying`

#### Scenario: Filter evaluated after commit
- **WHEN** an entity gains `Dying` and, in the same round, loses `Enemy`
- **THEN** a rule with `filter: Enemy` and `on added Dying` does not run for it

#### Scenario: Structural results are visible to the handler
- **WHEN** a handler adds `Dying` and, in the same round, spawns a bullet
- **THEN** the `on added Dying` handler can observe the committed bullet

### Requirement: Added binding and removed snapshot
In `on added T as x`, `x` SHALL name the entity's live `T`, with the same read and write
rules as a filter alias. In `on removed T as old`, `old` SHALL be a read-only value of type
`T` holding the trait's field values immediately before removal. A handler without `as`
SHALL have no binding for the trait.

#### Scenario: Removed snapshot holds the pre-removal value
- **WHEN** an entity with `Burning { intensity = 3.0 }` loses `Burning`, and a rule declares `on removed Burning as old`
- **THEN** `old.intensity` reads `3.0` in that handler

#### Scenario: Removed snapshot is read-only
- **WHEN** an `on removed Burning as old` handler assigns `old.intensity = 0.0`
- **THEN** the compiler reports a read-only assignment error

#### Scenario: Added binding writes like a filter alias
- **WHEN** an `on added Dying as dying` handler assigns `dying.elapsed = 0.0`
- **THEN** the assignment is accepted and appears as a write of `Dying.elapsed` in the handler's contract

### Requirement: Destroyed entities fire no lifecycle trigger
Destroying an entity SHALL NOT deliver `on removed` for any of its traits, and SHALL NOT
deliver any other lifecycle trigger for that entity. An entity spawned and destroyed within
the same round SHALL fire nothing.

#### Scenario: Destroy fires no removed
- **WHEN** an entity carrying `Burning` is destroyed, and a rule declares `on removed Burning`
- **THEN** that handler does not run for the destroyed entity

#### Scenario: Spawn and destroy in one round fire nothing
- **WHEN** one round spawns an `Enemy` entity and destroys it
- **THEN** no `on added Enemy` handler runs for it

### Requirement: Load-time entities fire added before load
At program start, once the entry module's `entity` declarations exist, the runtime SHALL
deliver `on added T` for every such entity and every watched trait it carries, before any
`on load` handler runs and before the first frame. Delivery order SHALL be entity creation
order, then trait canonical-identity order.

#### Scenario: Placed entity receives arrival setup
- **WHEN** an `entity Boss` declaration carries `Enemy`, and a rule declares `on added Enemy`
- **THEN** that handler runs once for `Boss` before the first frame

#### Scenario: Added runs before load
- **WHEN** a program has both an `on added Enemy` handler and an `on load` handler
- **THEN** every startup `on added Enemy` delivery completes before any `on load` handler runs

### Requirement: Deterministic delivery order
Within one round, lifecycle triggers SHALL be delivered in the order in which the round's
commands first changed each (entity, trait) pair, and for a single spawn, in trait
canonical-identity order. The same program and inputs SHALL produce the same delivery order.

#### Scenario: Spawned entity's traits fire in canonical order
- **WHEN** a spawned entity carries `A` and `B` and both have `on added` triggers
- **THEN** the triggers are delivered in the canonical-identity order of `A` and `B` on every run

### Requirement: Trigger cascades are bounded
Commands issued by lifecycle trigger handlers SHALL join the next commit round of the same
activation. Lifecycle deliveries SHALL count toward the activation's cascade-depth bound in
the same way as commit notifications; deliveries past the bound SHALL be deferred to a later
activation, together with any commands their handlers issue.

#### Scenario: Chained state changes commit in later rounds
- **WHEN** an `on added Dying` handler issues `add Fading`, and a rule declares `on added Fading`
- **THEN** `Fading` is added in the next round of the same activation and its trigger runs after that round

#### Scenario: Self-feeding triggers terminate
- **WHEN** an `on added Pulse` handler removes `Pulse`, and an `on removed Pulse` handler adds it back
- **THEN** the activation's commit still terminates, and the deliveries past the bound are deferred

### Requirement: Lifecycle trigger restrictions
The compiler SHALL reject `on added` / `on removed` when `T` is a projected trait, and in a
`pairs:` rule. World restore SHALL NOT fire lifecycle triggers.

#### Scenario: Projected trait rejected
- **WHEN** a rule declares `on added Hovered` and `Hovered` is a projected trait
- **THEN** the compiler reports that lifecycle triggers require a durable trait

#### Scenario: Pair rule rejected
- **WHEN** a `pairs:` rule declares `on added Dying`
- **THEN** the compiler reports that lifecycle triggers are not allowed in pair rules

#### Scenario: Restore fires nothing
- **WHEN** a world snapshot is restored that contains entities carrying watched traits
- **THEN** no lifecycle trigger runs as a result of the restore

### Requirement: No cost without triggers
A program that declares no lifecycle trigger SHALL NOT track trait presence changes at
commit, and SHALL generate no lifecycle delivery code. Only traits named by at least one
lifecycle trigger SHALL be tracked.

#### Scenario: Program without triggers is unaffected
- **WHEN** a program declares no `on added` or `on removed` handler
- **THEN** its generated commit path contains no lifecycle tracking or delivery

#### Scenario: Unwatched traits are not tracked
- **WHEN** a program declares only `on added Dying`
- **THEN** adding or removing any other trait does no lifecycle tracking work at commit

