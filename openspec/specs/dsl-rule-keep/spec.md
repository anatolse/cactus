# dsl-rule-keep Specification

## Purpose
Define kept traits: a rule names a trait that the compiler keeps on each recipient exactly while the rule's rows say so (an entity passing `where:`, a pair binding with a matching row, or a reduced group whose condition holds), so entering and leaving a fact (a state, a contact, a zone, a range) become ordinary `on added` / `on removed` triggers.
## Requirements
### Requirement: Keep clause shape

A rule SHALL accept one `keep` clause in one of three forms:
- On a unary `each:` rule without `reduce:`: `keep T`, with no `on`.
- On a pair rule without `reduce:`: `keep T on b`, where `b` is one of its pair bindings.
- On a pair rule with `reduce per b:`: `keep T on b`, where `b` is the `per` binding, optionally followed by `if condition`.

`T` SHALL be a durable trait declared in the same module as the rule, with no `persist` fields and not a state variant. The clause body SHALL assign zero or more fields of `T` from pure expressions; every field it leaves out SHALL take `T`'s declared default, and a field with no default SHALL be assigned. On a unary rule the expressions read the rule's filter traits and aliases, constants and named entity fields. On an unreduced pair rule they read only `b`, constants and named entity fields. On a reduced rule they read the aggregates, `b`, constants and named entity fields. A condition SHALL be a pure `bool` over the same scope as the fields. `keep` is recognized contextually at the rule-clause position.

The compiler SHALL reject, with a source-located error, `keep` on selectionless, globally reduced and extern rules; `on` on a unary rule; a pair keep without `on`; a binding other than the `per` binding on a reduced rule; `if` on a unary or unreduced pair rule; a field read outside the form's scope; and a field or condition whose type does not match. A read of the non-recipient binding on an unreduced pair keep SHALL be reported with help that suggests `reduce per` and `best(...)`.

#### Scenario: Valid keep clause
- **WHEN** a rule over `pairs: body, lava` uses `reduce per body:` with `n = count()` and declares `keep InLava on body:` with `zones = n`
- **THEN** compilation accepts it

#### Scenario: Wrong binding
- **WHEN** a rule with `reduce per body:` declares `keep InLava on lava:`
- **THEN** the compiler reports a source-located error naming the `per` binding

#### Scenario: Eliminated binding read
- **WHEN** a keep field expression on a reduced rule reads the eliminated `lava` binding
- **THEN** the compiler reports a source-located error

#### Scenario: Unary keep
- **WHEN** a rule with `each: Health as hp` and `where: hp.current < 30.0` declares `keep LowHealth`
- **THEN** compilation accepts it

#### Scenario: Unary keep names a recipient
- **WHEN** a unary rule declares `keep LowHealth on self`
- **THEN** the compiler reports an error at `on self` whose help says to remove it

#### Scenario: Unreduced rule
- **WHEN** a rule over `pairs: player, hurtbox` without `reduce:` declares `keep Caught on player`
- **THEN** compilation accepts it

#### Scenario: Unreduced pair keep without a recipient
- **WHEN** a pair rule without `reduce:` declares `keep Caught`
- **THEN** the compiler reports an error asking to name a pair binding with `on`

#### Scenario: Unreduced pair keep reads the other binding
- **WHEN** an unreduced pair keep on `player` assigns a field from `hurtbox[Hurtbox].damage`
- **THEN** the compiler reports a source-located error whose help suggests `reduce per player:` with `best(hurtbox, by = ...)`

#### Scenario: Condition on a reduced keep
- **WHEN** a rule with `reduce per owner:` and `n = count()` declares `keep EmptyInventory on owner if n == 0`
- **THEN** compilation accepts it

#### Scenario: Condition outside the reduced form
- **WHEN** a unary or unreduced pair keep declares `if` a condition
- **THEN** the compiler reports an error at the condition whose help says to use `where:`

#### Scenario: Invalid condition
- **WHEN** a keep condition has a type other than `bool`, or is impure
- **THEN** the compiler reports a source-located error

#### Scenario: Trait restrictions in every form
- **WHEN** a unary, unreduced pair or reduced keep names a state variant, a trait with `persist` fields, or a trait from another module
- **THEN** the compiler rejects it

### Requirement: Kept trait presence follows rows

At each run of a keep rule, the rule SHALL compute which recipients carry `T`:
- Unary form: every entity of the rule's domain that passes `where:`.
- Unreduced pair form: every entity of `b`'s membership with at least one row after `where:`.
- Reduced form without a condition: every group with at least one row after `where:`.
- Reduced form with a condition: every live group, including a group with no rows, whose condition is true.

Each such recipient SHALL carry `T` after the activation commit, with every assigned field set to this run's value. Every other carrier of `T`, including an entity that has left the domain or `b`'s membership, SHALL lose `T` at the commit. Adding and removing `T` SHALL happen at the activation commit, like `add` and `remove`, and SHALL fire `on added T` and `on removed T` triggers. Rewriting fields of a `T` that stays present SHALL fire no trigger. Changes SHALL be applied in creation order of the recipients.

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

#### Scenario: Unary fact follows where
- **WHEN** an entity's `Health.current` drops below 30 and later rises above it, under `keep LowHealth` with `where: hp.current < 30.0`
- **THEN** `LowHealth` is added at the commit of the first run below 30 and removed at the commit of the first run above it

#### Scenario: Unary entity leaves the domain
- **WHEN** an entity carrying `LowHealth` loses `Health`
- **THEN** the next run removes `LowHealth` from it

#### Scenario: Two witnesses give one fact
- **WHEN** a player touches two hurtboxes under `keep Caught on player`, then stops touching one of them
- **THEN** `on added Caught` fires once and no trigger fires when one hurtbox is left

#### Scenario: Unreduced pair fact ends with the last witness
- **WHEN** a player stops touching its last hurtbox
- **THEN** the next run removes `Caught` and `on removed Caught` fires

#### Scenario: Condition keeps an empty group
- **WHEN** an inventory owner has no items under `keep EmptyInventory on owner if n == 0`
- **THEN** `EmptyInventory` is present after the commit
- **AND** after the owner's first item appears, the next run removes it

#### Scenario: Threshold condition
- **WHEN** a unit has two allies in range, then three, under `keep Supported on unit if n >= 3`
- **THEN** `Supported` is absent with two and added at the commit of the run with three

#### Scenario: All conditions false
- **WHEN** every group's condition is false while earlier carriers exist
- **THEN** the run removes `T` from every carrier

### Requirement: Keep rules run on phase activations only

A keep rule SHALL run once per activation of the phase of its `group:`. A keep rule with no `group:` SHALL be rejected with a diagnostic suggesting a `group:`. A keep rule SHALL declare no handler: every handler, whatever its trigger, SHALL be rejected with a source-located error whose help says to move the body to another rule and react with `on added T` / `on removed T` or a filter on `T`. A keep rule SHALL NOT declare `order by:` or `limit:`; each SHALL be rejected with a source-located error that says the clause has no effect on `keep T` and suggests narrowing rows with `where:` or storing `best(...)` in a kept field. When `when:` is false, the rule SHALL NOT run and every kept trait SHALL keep its presence and values. A rendered frame with zero activations of the phase SHALL change nothing.

#### Scenario: Handler-less keep rule
- **WHEN** a keep rule declares `group: physics.contacts` and no handler
- **THEN** it runs once per `fixed_tick` in that group's place

#### Scenario: Missing phase
- **WHEN** a keep rule has no `group:`
- **THEN** the compiler reports an error that suggests adding a `group:`

#### Scenario: Keep rule with a handler
- **WHEN** a keep rule declares `on fixed_tick:` or `on added InLava:`
- **THEN** the compiler reports an error at the handler that names `InLava` and suggests moving the body to another rule

#### Scenario: Keep rule with order by or limit
- **WHEN** a keep rule declares `order by:` or `limit:`
- **THEN** the compiler reports an error at that clause saying it has no effect on the kept trait

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

### Requirement: Keep rules don't depend on themselves

A keep rule depends on another keep rule when any of its clauses reads the trait the other keeps: a domain filter (`each:`, `without:`, a pair binding's traits), `where:`, a reducer input, a field value, the condition, or `when:`, including named entity reads. These dependencies SHALL form no cycle across the linked program. A keep rule that reads its own kept trait SHALL count as a cycle. Each cycle SHALL be reported as a compile error that names every keep rule in it with its source location. This enforces determinacy of a kept trait as a function of other data.

#### Scenario: Keeper reads its own trait
- **WHEN** a unary rule with `without: Lit` declares `keep Lit`
- **THEN** the compiler reports an error that names the rule and says it reads its own kept trait `Lit`

#### Scenario: Two keepers read each other
- **WHEN** rule `AFact` keeps `A` and filters `without: B`, and rule `BFact` keeps `B` and filters on `A`
- **THEN** the compiler reports one error naming `AFact` and `BFact`

#### Scenario: Chain is accepted
- **WHEN** `LowHealthFact` keeps `LowHealth` from `Health`, and `CanEscapeFact` keeps `CanEscape` and filters on `LowHealth`
- **THEN** compilation accepts both

#### Scenario: Cycle across modules
- **WHEN** a keep rule in one module reads a trait kept in an imported module whose keeper reads the first rule's trait
- **THEN** the two modules import each other, and the compiler reports the import cycle

