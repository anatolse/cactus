# dsl-where-clause Specification

## Purpose
Define the `where:` clause: a pure boolean predicate list that restricts a regular rule's unary (`filter:`) or pair (`pairs:`) domain before its handler executes, independent of any particular execution strategy.

## Requirements

### Requirement: `where:` restricts an existing unary or pair domain
A regular rule that declares `filter:` and/or `pairs:` SHALL accept an optional `where:` block containing one or more boolean predicate expressions. Multiple predicate lines SHALL form an unordered logical conjunction: every line MUST evaluate to `true` for the entity or tuple to remain in the domain. `where:` SHALL be rejected on rules with neither `filter:` nor `pairs:` and on `extern rule` declarations.

#### Scenario: where: restricts a unary filter domain
- **WHEN** a rule declares `filter: Ball as ball` and `where: ball.velocity.x > 0.0`
- **THEN** the handler executes only for entities satisfying both the filter and the predicate

#### Scenario: where: restricts a pair domain
- **WHEN** a rule declares `pairs:` bindings `a`/`b` and `where: a != b`
- **THEN** the handler executes only for tuples satisfying both the pair domain and the predicate

#### Scenario: Multiple predicate lines form a conjunction
- **WHEN** `where:` contains two lines, `a != b` and `distance(a, b) < 1.0`
- **THEN** a tuple executes only when both lines evaluate to `true`

#### Scenario: where: without filter or pairs is rejected
- **WHEN** a rule declares `where:` but neither `filter:` nor `pairs:`
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
`where:` SHALL be evaluated once per entity or tuple, at the start of that entity's or tuple's handler invocation, against the domain membership already determined by `filter:`/`exclude:`/`pairs:`. `where:` MAY only remove entities or tuples from that membership; it MUST NOT add any and MUST NOT be re-evaluated mid-pass as a result of mutations, projections, or buffered structural commands performed by earlier invocations in the same pass.

#### Scenario: Rejected entities do not run the handler body
- **WHEN** an entity satisfies `filter:` but not `where:`
- **THEN** its handler body does not execute for that activation

#### Scenario: where: cannot expand the pass
- **WHEN** an earlier invocation in the same pass performs a mutation that would make another entity newly satisfy a `where:` predicate
- **THEN** that other entity's membership for the current pass is unaffected
