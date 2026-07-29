# handler-contracts Specification

## Purpose
Define per-handler selection, inferred and declared access/effect contracts, contract validation, and selectionless execution semantics for regular and external handlers.

## Requirements

### Requirement: Contracts belong to individual handlers
Every regular or external handler SHALL have an independent contract containing its trigger, selection, reads, writes, emitted events, structural commands, and external effect domains. A system-level filter/exclude/order definition SHALL be copied into each handler node but SHALL NOT merge the handlers' access contracts.

#### Scenario: Multi-handler system has distinct contracts
- **WHEN** `Player` handles fixed_tick and Damaged with different behavior
- **THEN** the semantic representation contains distinct contracts for `Player.fixed_tick` and `Player.Damaged`

### Requirement: Regular handler contracts are inferred
The semantic analyzer SHALL infer a regular handler's contract from all reachable statements and expressions. Immutable trait field access SHALL add `reads`; mutation SHALL add `writes`; `writes` SHALL mean read/write access; event emission SHALL add `emits`; structural statements SHALL add the corresponding `commands`; and calls to effectful extern functions SHALL add their known effect domain or conservative `external` effect.

#### Scenario: Mutation infers read-write access
- **WHEN** a regular handler assigns through filter alias `transform`
- **THEN** its contract contains the canonical Transform trait in `writes`

#### Scenario: Event and command use are inferred
- **WHEN** a handler emits Contact and spawns Particle
- **THEN** its contract contains Contact in `emits` and `spawn Particle` in `commands`

### Requirement: External handlers declare complete contracts
An `extern system` handler SHALL declare zero or more `reads:`, `writes:`, `emits:`, `commands:`, and `effects:` blocks because its implementation is unavailable for inference. Trait entries in `reads` and `writes` SHALL resolve through that system's filter aliases or canonical trait references. Event and command entries SHALL resolve canonically.

#### Scenario: External movement contract resolves aliases
- **WHEN** NativeMovement filters `Motion as motion` and `Transform as transform`, reads motion, and writes transform
- **THEN** its handler contract stores canonical Motion in reads and canonical Transform in writes

#### Scenario: Unknown contract alias is rejected
- **WHEN** an external handler lists `velocity` under reads but no filter binding or trait reference resolves it
- **THEN** semantic analysis reports the unknown contract entry

### Requirement: Contracts constrain external implementations
Generated external-handler APIs SHALL expose immutable access for `reads`, mutable access for `writes`, event emission only for `emits`, structural operations only for `commands`, and effect services only for declared `effects`. Backends MUST NOT grant undeclared world mutation through the generated contract API.

#### Scenario: Read entry is immutable
- **WHEN** an external handler lists Sprite only under reads
- **THEN** its generated C++ callback receives const Sprite access

#### Scenario: Write entry is mutable
- **WHEN** an external handler lists Transform under writes
- **THEN** its generated C++ callback receives mutable Transform access

### Requirement: Selection does not imply trait access
`filter:` and `exclude:` SHALL determine entity selection without adding reads or writes. An `order by:` expression SHALL add a read for every trait whose data it evaluates.

#### Scenario: Filter-only trait is not read
- **WHEN** a handler filters Disabled as a marker and never accesses its fields
- **THEN** Disabled is absent from the handler's reads and writes

#### Scenario: Sort key adds read
- **WHEN** a system orders by `transform.position.x`
- **THEN** each affected handler contract includes Transform in reads

### Requirement: Selectionless handler cardinality
A handler with neither positive filter entries nor exclude entries SHALL execute once per phase activation or delivered event occurrence. A handler with any filter or exclude selection clause SHALL execute once per selected entity.

#### Scenario: Filterless input producer runs once
- **WHEN** InputSource has a selectionless `on input` handler
- **THEN** it runs once for the input activation regardless of entity count

#### Scenario: Exclude-only cleanup remains an entity pass
- **WHEN** a handler has only `exclude: Persistent`
- **THEN** it executes for every non-Persistent selected entity rather than once globally

### Requirement: Command vocabulary and validation
Handler commands SHALL use the forms `spawn Template`, `destroy`, `add Trait`, and `remove Trait`. Regular inference and external declarations SHALL store canonical template/trait identities, and an external implementation MUST NOT issue a command absent from its handler contract.

#### Scenario: Contracted structural changes validate
- **WHEN** an external handler declares `spawn Particle`, `destroy`, `add Disabled`, and `remove Active`
- **THEN** all names resolve and the four operations are available to that callback
