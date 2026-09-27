# dsl-named-entity-access Specification

## Purpose

Let handler code refer to a declared entity by its name, both as an `entity_id` value and through
direct `Name.Trait.field` reads and writes, so games can hold game-wide state on an ordinary named
entity without marker-trait queries.

## Requirements

### Requirement: A declared entity's name is an entity_id value
The name of an entity declared with `entity Name:` or `entity Name from Template:` SHALL be an
expression of type `entity_id` in handler bodies, in rule clauses, and in archetype-body override
values. Its value SHALL be the entity's current binding (see "The name follows its entity's
lifecycle"). A `pub entity` name SHALL be usable from importing modules through the module
qualifier, like other public symbols. Using a name as a value SHALL NOT add any requirement to the
handler.

#### Scenario: Name used as a set target
- **WHEN** a handler runs `set text.ScreenLabel on CrosshairHud:` with `visible = false`, and
  `CrosshairHud` is a declared entity carrying `ScreenLabel`
- **THEN** after the activation commit `CrosshairHud`'s `ScreenLabel.visible` is `false`

#### Scenario: Name used as an emit target and in comparisons
- **WHEN** a handler runs `emit Hit to Boss` and evaluates `target == Boss`
- **THEN** the event is delivered to the `Boss` entity, and the comparison is true exactly when
  `target` is `Boss`'s current binding

#### Scenario: Stale name as a value is a no-op target
- **WHEN** `Boss` was destroyed and a handler runs `set Health on Boss:` or `emit Hit to Boss`
- **THEN** the handler still runs, the `set` does nothing, and the event is not delivered

#### Scenario: Name as an archetype override value
- **WHEN** `entity Nemesis:` has the override `Rival: rival = Boss` and `Boss` is declared in the same
  module, before or after `Nemesis`
- **THEN** the program compiles, and after load `Nemesis`'s `Rival.rival` equals `Boss`'s binding

#### Scenario: Template names are not values
- **WHEN** a handler uses a `template` name as an expression
- **THEN** semantic analysis reports that a template is not an `entity_id` value

### Requirement: Named field access reads and writes a named entity's traits
A handler body or rule clause SHALL accept `Name.Trait.field` (and deeper member paths such as
`Game.Transform.position.x`), where `Name` is a declared entity and `Trait` is a trait the entity's
archetype declares, including through an import qualifier (`Hud.text.ScreenLabel.visible`). A read
SHALL return the field's current value. In a handler body, assignment and compound assignment to
such a path SHALL write immediately, like a write through `self`. A trait the archetype does not
declare SHALL be a compile error. Named field access SHALL be rejected in `func` bodies, `const`
blocks, and archetype bodies.

#### Scenario: Read game-wide state by name
- **WHEN** entity `Game` declares `Match` with `over = false`, and a handler reads `Game.Match.over`
- **THEN** the read yields `false`

#### Scenario: Immediate write by name is visible to later handlers
- **WHEN** a handler runs `Game.Match.score += 1`, and a handler scheduled later in the same activation
  reads `Game.Match.score`
- **THEN** the later handler observes the incremented value

#### Scenario: Undeclared trait is rejected
- **WHEN** a handler reads `Game.Health.value` and `Game`'s archetype does not declare `Health`
- **THEN** semantic analysis reports that `Game` does not declare `Health`

#### Scenario: Named access in a func body is rejected
- **WHEN** a `func` body reads `Game.Match.over`
- **THEN** semantic analysis reports that named field access is only allowed in rules

### Requirement: Named field access implicitly requires the entity and trait
For every `(Name, Trait)` pair that a handler accesses by name, whether in its body or in any clause of its
rule, the handler SHALL run a pass only when `Name`'s current binding is alive and carries `Trait`.
The runtime SHALL evaluate this requirement once per pass: once per phase activation of the
handler, or once per delivered event occurrence, before any entity or tuple is visited and
before `when:`. If it fails, that pass SHALL do nothing: no body execution, no writes, no commands.
The requirement SHALL cover the whole handler, even when the name appears only in one branch.
Because `destroy` and `remove` are buffered until the activation commit, a requirement that holds
at the start of a pass SHALL hold for the whole pass.

#### Scenario: Handler stops when the named entity is destroyed
- **WHEN** a filter rule's handler reads `Game.Match.over`, and `Game` is destroyed at a commit
- **THEN** in later activations that handler does not run for any entity, and no error is raised

#### Scenario: Handler stops when the named trait is removed
- **WHEN** `remove Match from Game` commits
- **THEN** handlers that access `Game.Match` by name stop running, and handlers that access only
  other traits of `Game` by name keep running

#### Scenario: Requirement covers the whole handler
- **WHEN** a handler reads `Game.Match.over` only inside one `if` branch, and `Game` is not alive
- **THEN** the handler's pass does nothing, including the statements outside that branch

#### Scenario: Destroy inside the pass does not break later reads
- **WHEN** a handler runs `destroy Game` and then reads `Game.Match.over` in the same pass
- **THEN** the read yields the current value, and `Game` is removed at the activation commit

### Requirement: The name follows its entity's lifecycle
A name SHALL bind to the instance created when its declaring module's entities are instantiated:
at program start for the entry module, and at each scene load for a loaded module. Every
declared entity of a module SHALL have its handle allocated before any of that module's archetype
initializers run, so names used as override values may refer to entities declared later in the
module, or to each other. Once the bound instance is destroyed, including by scene cleanup when
it lacks `KeepOnLoad`, the name SHALL be stale until the declaring module instantiates it again.
After a restore, the name SHALL bind to the restored record whose archetype origin is the entity's
declaration, or be stale if the document has no such record. Named entities SHALL be captured
and restored like any other entity, and their `persist` fields SHALL be restored normally.

#### Scenario: Mutually referencing named entities load
- **WHEN** `entity A:` overrides `Link: other = B` and `entity B:` overrides `Link: other = A`
- **THEN** after load `A.Link.other` equals `B`'s binding and `B.Link.other` equals `A`'s binding

#### Scenario: Restore rebinds the name
- **WHEN** a snapshot containing `Game` with a `persist` field `Match.best = 7` is restored
- **THEN** `Game` binds to the restored instance, `Game.Match.best` reads `7`, and handlers that
  access `Game.Match` by name run

#### Scenario: Restore without the entity leaves the name stale
- **WHEN** a restored document has no record whose origin is `Game`
- **THEN** `Game` is stale, handlers that access `Game` fields by name do not run, and
  `set`/`emit` targeting `Game` do nothing

### Requirement: Named writes are never split per entity
A handler that writes a named entity's field SHALL be treated by every backend as one sequential
pass over its domain: its iterations SHALL run in the domain's deterministic order, and a backend
SHALL NOT run them in parallel.

#### Scenario: Accumulating into a named field is deterministic
- **WHEN** a filter rule over 100 `Coin` entities runs `Game.Match.score += coin.value`
- **THEN** after the pass `Game.Match.score` equals the previous score plus the sum of all coin values
