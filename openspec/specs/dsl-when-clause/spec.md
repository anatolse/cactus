# dsl-when-clause Specification

## Purpose

Define the rule-level `when:` clause: a pure boolean gate over game-wide state, evaluated once per
handler pass, that lets a whole rule switch off (for example while the game is over) without a
per-entity check.

## Requirements

### Requirement: `when:` gates every handler of a rule
A regular rule SHALL accept an optional `when:` block containing one or more boolean predicate
expressions, forming a logical conjunction. It SHALL apply to every handler of the rule, including
event handlers and rules without `filter:` or `pairs:`. When any predicate is false, the handler's
pass SHALL do nothing: no entity or tuple is visited, the body does not run, and no writes or
commands are produced. `when:` SHALL be rejected on `extern rule` declarations.

#### Scenario: Gate off skips the whole filter pass
- **WHEN** a rule declares `filter: Enemy` and `when: not Game.Match.over`, and `Game.Match.over` is
  true
- **THEN** the handler body runs for no `Enemy`

#### Scenario: Gate applies to event handlers
- **WHEN** a rule with `when: not Game.Match.over` handles `on GameOver:`, and the gate is false when an
  occurrence is delivered
- **THEN** that occurrence does not run the handler

#### Scenario: Multiple predicate lines form a conjunction
- **WHEN** `when:` contains `not Game.Match.over` and `Game.Match.wave > 0`
- **THEN** the handler runs only when both are true

#### Scenario: when: on an extern rule is rejected
- **WHEN** an `extern rule` declares `when:`
- **THEN** compilation reports that `when:` is only allowed on regular rules

### Requirement: `when:` predicates are pure and read only game-wide values
Each `when:` predicate SHALL have static type `bool`. It MAY contain literals, constants, named
field reads (`Name.Trait.field`), entity names as values, operators, and calls to functions whose
call graph is proven pure. It MUST NOT read filter, pair, or `self` bindings, event payloads, or
handler locals, and it MUST NOT contain any effect that a `where:` predicate forbids.

#### Scenario: Binding read in when: is rejected
- **WHEN** a rule with `filter: Enemy as enemy` declares `when: enemy.dying`
- **THEN** semantic analysis reports that `when:` cannot read a binding, and suggests `where:`

#### Scenario: Non-bool predicate is rejected
- **WHEN** a `when:` line has type `int`
- **THEN** semantic analysis reports a type error for that predicate

#### Scenario: World query in when: is rejected
- **WHEN** a `when:` predicate calls `query.first[Player]()`
- **THEN** semantic analysis reports that `when:` predicates must be pure

### Requirement: `when:` evaluates once per pass
`when:` SHALL be evaluated once per pass (once per phase activation of the handler, or once per
delivered event occurrence) after the implicit named-entity requirement holds and before any
entity or tuple is visited. It SHALL observe immediate writes made earlier in the same activation
by handlers scheduled before it. Its result SHALL NOT be re-evaluated during the pass.

#### Scenario: Earlier writer in the same activation is observed
- **WHEN** handler A writes `Game.Match.over = true`, and handler B's rule has `when: not Game.Match.over`
  and reads nothing else from A
- **THEN** A is scheduled before B, and B's pass in that activation does nothing

#### Scenario: A write during the pass does not re-open or close the gate
- **WHEN** a handler whose `when:` is true sets `Game.Match.over = true` while visiting the first of
  three entities
- **THEN** the handler still visits all three entities in that pass

### Requirement: `when:` names count as named field access
Every named field read in `when:` SHALL add the same implicit requirement as a named read in the
handler body, for every handler of the rule, and SHALL be folded into each handler's contract as
a read.

#### Scenario: Missing entity in when: stops the rule
- **WHEN** a rule has `when: not Game.Match.over` and `Game` is stale
- **THEN** none of the rule's handlers run
