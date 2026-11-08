## Purpose

Define which query shapes the compiler reports as unaccelerated, so a superlinear scan or a full scan for one key is never silent. Diagnostics describe cost; they never change behavior.

## Requirements

### Requirement: A world scan is any call that visits every match
A world scan SHALL be a call whose cost grows with the number of entities its filter can match: `query.all`, `query.first`, `query.count`, `query.hierarchy_preorder`, `query.hierarchy_postorder`, and the `nearest`, `overlap_*` and `raycast` queries of the physics modules. `query.exists`, `query.parent` and `query.children` SHALL NOT be world scans. A scan SHALL count as bounded when only named entities can match its filter, or when its filter requires a `single` trait.

#### Scenario: Count is a world scan
- **WHEN** a handler calls `query.count[Unit]()` and `Unit` can be carried by spawned entities
- **THEN** the call is an unbounded world scan

#### Scenario: A named-only filter is bounded
- **WHEN** a handler calls `query.first[Session]()` and only named entities can carry `Session`
- **THEN** the call is a bounded scan

#### Scenario: A singleton filter is bounded
- **WHEN** a handler calls `query.first[Terrain]()`, `Terrain` is declared `single`, and spawned entities carry it
- **THEN** the call is a bounded scan

### Requirement: Superlinear scans are warned
The compiler SHALL warn, naming the rule and the scan's source location, when an unbounded world scan runs on either of these paths:
1. Once per row of a unary or pair pass triggered by a phase, when the pass's domain is not bounded by named entities or a `single` trait.
2. Inside a `for` loop over another unbounded world scan, for any trigger.

A warning SHALL be reported once per scan call. The check SHALL run on the whole linked program, because boundedness depends on every template and spawn.

#### Scenario: Per-row scan
- **WHEN** a `tick` rule has `each: Unit` and its body calls `query.count[Unit]()`, and spawned entities carry `Unit`
- **THEN** the compiler warns and names the rule

#### Scenario: Nested scan loop
- **WHEN** a handler with no domain runs `for a in query.all[Unit]():` and that loop calls `query.count[Unit]()`
- **THEN** the compiler warns and names the rule

#### Scenario: One scan per activation is linear
- **WHEN** a handler with no domain calls `query.all[Unit]()` once
- **THEN** no warning is reported

#### Scenario: A named row is not a multiplier
- **WHEN** a `tick` rule's domain can only match named entities and its body calls `query.all[Unit]()`
- **THEN** no warning is reported

#### Scenario: Event handlers are not multiplied
- **WHEN** a rule with `each: Unit` handles an ordinary event and calls `query.count[Unit]()`
- **THEN** no warning is reported, because the rows depend on how the event is delivered

### Requirement: A full scan for a key is warned
The compiler SHALL warn, naming the rule and the predicate, when a unary `where:` has a top-level conjunct that compares `self` or a field of a domain trait with `==` to an expression that does not read the row, and the domain is not bounded by named entities or a `single` trait. A unary `where:` with no such conjunct SHALL NOT be warned, because one visit per row is the cost of the pass.

#### Scenario: Key lookup
- **WHEN** a rule has `each: Unit as u` and `where: u.id == W[Want].id`, and spawned entities carry `Unit`
- **THEN** the compiler warns and names the rule

#### Scenario: Range filter
- **WHEN** a rule has `each: Health as hp` and `where: hp.current < 30.0`
- **THEN** no warning is reported

#### Scenario: Comparing two row fields
- **WHEN** a rule has `each: Unit as u` and `where: u.team == u.owner`
- **THEN** no warning is reported

### Requirement: The standard library is free of warnings
Compiling a program that uses any standard library module SHALL report no query cost warning located in a standard library file. The 3D editor example SHALL compile with `--warnings-as-errors`, because it fills the editor's pair rules from real templates.

#### Scenario: A stdlib rule introduces a scan
- **WHEN** a stdlib rule would trigger a query cost warning
- **THEN** a guard test fails until the rule is fixed

#### Scenario: The editor example is clean
- **WHEN** `examples/editor-3d` is compiled with `--warnings-as-errors`
- **THEN** the build passes
