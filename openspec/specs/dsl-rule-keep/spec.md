# dsl-rule-keep Specification

## Purpose
Define kept traits: a reduced pair rule names a marker trait that the compiler keeps on each group entity while the group has rows, so entering and leaving a relation (a contact, a zone, a range) become ordinary `on added` / `on removed` triggers.
## Requirements
### Requirement: Keep clause shape

A pair rule with `reduce:` and `per: b` SHALL accept one `keep T on b:` clause, written after `reduce:`. `b` SHALL be the `per` binding. `T` SHALL be a durable trait declared in the same module as the rule, with no `persist` fields. The clause body SHALL assign zero or more fields of `T` from expressions that read only aggregates, the `per` binding, constants and named entity fields; every field it leaves out SHALL take `T`'s declared default. `keep` is recognized contextually at the rule-clause position. The compiler SHALL reject `keep` on unary, selectionless, unreduced, globally reduced and extern rules, on a binding other than the `per` binding, and with a field or type that does not match `T`.

#### Scenario: Valid keep clause
- **WHEN** a rule over `pairs: body, lava` reduces `per: body` with `n = count()` and declares `keep InLava on body:` with `zones = n`
- **THEN** compilation accepts it

#### Scenario: Wrong binding
- **WHEN** a rule reduced `per: body` declares `keep InLava on lava:`
- **THEN** the compiler reports a source-located error naming the `per` binding

#### Scenario: Eliminated binding read
- **WHEN** a keep field expression reads the eliminated `lava` binding
- **THEN** the compiler reports a source-located error

#### Scenario: Unreduced rule
- **WHEN** a pair rule without `reduce:` declares `keep`
- **THEN** the compiler rejects it

### Requirement: Kept trait presence follows rows

At each run of a keep rule, for every group entity `e`: if the group has at least one row after `where:`, `T` SHALL be present on `e` after the activation commit, with every assigned field set to this run's value. If the group has no row, or `e` has left the `per` binding's membership, `T` SHALL be absent from `e` after the commit. Adding and removing `T` SHALL happen at the activation commit, like `add` and `remove`, and SHALL fire `on added T` and `on removed T` triggers. Rewriting fields of a `T` that stays present SHALL fire no trigger. Changes SHALL be applied in group creation order.

#### Scenario: Enter fires once
- **WHEN** a body starts touching one lava zone and stays three ticks
- **THEN** `on added InLava` fires once, after the first tick's commit

#### Scenario: Overlapping zones
- **WHEN** a body in zone A also enters zone B, then leaves A while still in B
- **THEN** no trigger fires, and `InLava.zones` reads 1, then 2, then 1

#### Scenario: Exit carries the last value
- **WHEN** a body leaves its last lava zone
- **THEN** `on removed InLava as old` fires once with `old.zones` equal to the last kept value

#### Scenario: Zone destroyed
- **WHEN** the only zone a body touches is destroyed
- **THEN** the next run removes `InLava` from the body and `on removed InLava` fires

#### Scenario: Body leaves the binding
- **WHEN** a body loses a trait the `per` binding requires while it carries `InLava`
- **THEN** the next run removes `InLava` from it

#### Scenario: Kept entity destroyed
- **WHEN** an entity carrying a kept trait is destroyed
- **THEN** no trigger fires for it

### Requirement: Keep rules run on phase activations only

A keep rule SHALL run once per activation of its phase. Its phase SHALL come from its phase-trigger handlers when it has any, otherwise from its `group:`; a keep rule with neither SHALL be rejected with a diagnostic suggesting a `group:`. Event and lifecycle handlers SHALL be rejected on a keep rule. When `when:` is false, the rule SHALL NOT run and every kept trait SHALL keep its presence and values. A rendered frame with zero activations of the phase SHALL change nothing.

#### Scenario: Handler-less keep rule
- **WHEN** a keep rule declares `group: physics.contacts` and no handler
- **THEN** it runs once per `fixed_tick` in that group's place

#### Scenario: Missing phase
- **WHEN** a keep rule has no handler and no `group:`
- **THEN** the compiler reports an error that suggests adding a `group:`

#### Scenario: Paused by when
- **WHEN** a keep rule's `when:` is false while a body carries its kept trait and then leaves the zone
- **THEN** the trait stays present until `when:` is true again and the rule runs

### Requirement: Only the keep rule changes a kept trait

A program SHALL have at most one `keep` clause per trait. Every other write to a kept trait SHALL be rejected at compile time: direct field assignment, `set`, `add`, `remove`, `project`, and spawning or declaring an entity with it. Reading a kept trait, filtering on it and reacting with `on added` / `on removed` SHALL be allowed.

#### Scenario: Two keep rules
- **WHEN** two rules both declare `keep InLava on ...`
- **THEN** the compiler reports an error naming both rules

#### Scenario: Manual add
- **WHEN** a handler writes `add InLava`
- **THEN** the compiler reports that `InLava` is kept by `TouchLava`

#### Scenario: Read is allowed
- **WHEN** a rule filters on `InLava as lava` and reads `lava.dps`
- **THEN** compilation accepts it

