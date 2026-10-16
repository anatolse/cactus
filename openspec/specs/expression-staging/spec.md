# expression-staging Specification

## Purpose
Gives every rule clause expression the earliest stage at which its value is fixed for a pass, so a backend can evaluate it once per activation or once per binding member instead of once per row, without changing which rows run.
## Requirements
### Requirement: Rule clause expressions have a stage
The compiler SHALL assign each `where:` predicate, `order by:` key and reducer input of a regular rule exactly one stage:
- **activation**: the expression reads no binding (no filter alias or implicit filter field, no pair binding); it may read constants, literals and named entity fields;
- **binding**: in a pair rule, the expression reads exactly one pair binding (with its aliases);
- **row**: every other expression, including any expression of a unary rule that reads the selected entity, and any pair expression that reads both bindings.

The stage SHALL depend only on what the expression reads, never on source order. A `reduce per b:` group predicate SHALL be classified like any other predicate; its binding stage is the existing group filter.

#### Scenario: Pair predicate reading one binding
- **WHEN** a pair rule with bindings `body` and `lava` has `where: lava[Lava].active`
- **THEN** the predicate has binding stage for `lava`

#### Scenario: Predicate reading no binding
- **WHEN** a pair rule has `where: Game[Match].running`
- **THEN** the predicate has activation stage

#### Scenario: Join predicate
- **WHEN** a pair rule has `where: physics.touching(body, lava)`
- **THEN** the predicate has row stage

#### Scenario: Unary predicate on the selected entity
- **WHEN** a unary rule with `each: Health as h` has `where: h.hp > 0`
- **THEN** the predicate has row stage

### Requirement: An expression is lifted only when the pass can't change its inputs
An expression SHALL keep its classified stage only if no handler invocation of the same pass that can run before the expression is evaluated writes any field it reads, by assignment, compound assignment, accumulation, a named write, or a whole-trait write. A trait read whose field the compiler can't name SHALL conflict with any write to that trait. Otherwise the expression SHALL have row stage. Deferred commands (`set`, `add`, `remove`, `spawn`, `destroy`) and emitted events SHALL NOT count as writes for this rule, because nothing they change is visible before the pass ends. A pass evaluates its `order by:` keys and reducer inputs, and a pass with `limit:` or `reduce:` also evaluates its `where:` predicates, before its first invocation, so its writes never demote those expressions.

#### Scenario: Named write blocks lifting
- **WHEN** a unary rule has `where: Game[Score].open` and its handler writes `Game[Score].open = false`
- **THEN** the predicate has row stage and is evaluated at the start of each invocation, with live values

#### Scenario: Deferred patch does not block lifting
- **WHEN** a pair rule has `where: lava[Lava].active` and its handler runs `set Lava on lava: active = false`
- **THEN** the predicate keeps binding stage

#### Scenario: A reduced pass's writes do not block lifting
- **WHEN** a `reduce per team:` rule has `where: team[Team].total < 10` and its handler writes `team[Team].total`
- **THEN** the predicate keeps binding stage, because every row is folded before the handler runs

#### Scenario: Unnamed field read blocks lifting
- **WHEN** a predicate reads a trait through a path whose field the compiler can't name, and the handler writes any field of that trait
- **THEN** the predicate has row stage

### Requirement: Stages are part of the compiled program
The compiled program SHALL record the final stage of every `where:` predicate, `order by:` key and reducer input, per handler, so a backend can read them without recomputing. A backend MAY evaluate an expression at its recorded stage or at any later stage; doing so SHALL NOT change which entities or tuples run, their order, `limit:` slots, aggregates, or `reduce per` groups.

#### Scenario: Backend reads the stage
- **WHEN** a program with a pair rule is compiled
- **THEN** each of the rule's `where:` predicates has a recorded stage

#### Scenario: Evaluating later is always correct
- **WHEN** a backend evaluates a binding-stage predicate once per tuple instead of once per binding member
- **THEN** the same tuples run in the same order
