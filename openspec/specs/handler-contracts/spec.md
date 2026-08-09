# handler-contracts Specification

## Purpose
Define per-handler selection, inferred and declared access/effect contracts, contract validation, and selectionless execution semantics for regular and external handlers.

## Requirements

### Requirement: Contracts belong to individual handlers
Every regular or external handler SHALL have an independent contract containing its trigger, selection, reads, writes, emitted events, structural commands, and external effect domains. A rule-level filter/exclude/order definition SHALL be copied into each handler node but SHALL NOT merge the handlers' access contracts.

#### Scenario: Multi-handler rule has distinct contracts
- **WHEN** `Player` handles fixed_tick and Damaged with different behavior
- **THEN** the semantic representation contains distinct contracts for `Player.fixed_tick` and `Player.Damaged`

### Requirement: Handler contracts declare one execution domain
Every handler contract SHALL contain exactly one domain: selectionless, unary relation, or pair relation. A unary relation SHALL contain positive and excluded canonical trait identities. A pair relation SHALL contain exactly two ordered named bindings and each binding's required canonical traits.

#### Scenario: Unary contract remains unary
- **WHEN** a rule filters Position and Velocity
- **THEN** each handler contract records one unary relation rather than a pair or selectionless domain

#### Scenario: Pair contract preserves binding roles
- **WHEN** a pair rule binds `projectile` and `target`
- **THEN** every handler contract preserves those names, order, and separate required trait sets

### Requirement: Pair contracts record binding-qualified reads
A pair handler contract SHALL record each trait read with its relation binding and SHALL also expose the conservative union of canonical read traits for scheduling against existing handlers. Pair selection SHALL determine membership without adding reads.

#### Scenario: Bound reads distinguish roles
- **WHEN** a pair handler reads Collider on both `body` and `wall`
- **THEN** its precise contract contains two bound reads and its conservative read set contains Collider

### Requirement: Regular handler contracts are inferred
The semantic analyzer SHALL infer a regular handler's contract from all reachable statements and expressions. Immutable trait field access SHALL add `reads`; mutation SHALL add `writes`; `writes` SHALL mean read/write access; event emission SHALL add `emits`; structural statements SHALL add the corresponding `commands`; and calls to effectful extern functions SHALL add their known effect domain or conservative `external` effect.

#### Scenario: Mutation infers read-write access
- **WHEN** a regular handler assigns through filter alias `transform`
- **THEN** its contract contains the canonical Transform trait in `writes`

#### Scenario: Event and command use are inferred
- **WHEN** a handler emits Contact and spawns Particle
- **THEN** its contract contains Contact in `emits` and `spawn Particle` in `commands`

### Requirement: Projected outputs are distinct contract capabilities
Handler contracts SHALL record projected trait outputs separately from durable writes. Scheduling SHALL treat projection as production of that trait for ordered later matching and reads, while pair read-only validation SHALL continue to prohibit durable writes through pair bindings.

#### Scenario: Project output creates producer conflict information
- **WHEN** one handler projects Contacting and a later handler filters and reads Contacting
- **THEN** contract graph construction can order the projection producer before the consumer without classifying projection as a durable component mutation

### Requirement: External handlers declare complete contracts
An `extern rule` handler SHALL declare zero or more `reads:`, `writes:`, `projects:`, `emits:`, `commands:`, and `effects:` blocks because its implementation is unavailable for inference. Trait entries in `reads`, `writes`, and `projects` SHALL resolve through that rule's filter aliases or canonical trait references. Event and command entries SHALL resolve canonically. Projected outputs SHALL remain distinct from durable writes, and the same canonical trait SHALL NOT appear in both `writes` and `projects` for one handler.

#### Scenario: External movement contract resolves aliases
- **WHEN** NativeMovement filters `Motion as motion` and `Transform as transform`, reads motion, and writes transform
- **THEN** its handler contract stores canonical Motion in reads and canonical Transform in writes

#### Scenario: External projection contract resolves trait
- **WHEN** ExternalLayout lists `ComputedLayout` under projects
- **THEN** its handler contract stores canonical ComputedLayout in projected outputs rather than durable writes

#### Scenario: Unknown contract alias is rejected
- **WHEN** an external handler lists `velocity` under reads but no filter binding or trait reference resolves it
- **THEN** semantic analysis reports the unknown contract entry

### Requirement: Contracts constrain external implementations
Generated external-handler APIs SHALL expose immutable access for `reads`, mutable durable access for `writes`, frame-local projection capability only for `projects`, event emission only for `emits`, structural operations only for `commands`, and effect services only for declared `effects`. Backends MUST NOT grant undeclared world mutation or projection through the generated contract API.

#### Scenario: Read entry is immutable
- **WHEN** an external handler lists Sprite only under reads
- **THEN** its generated C++ callback receives const Sprite access

#### Scenario: Write entry is mutable
- **WHEN** an external handler lists Transform under writes
- **THEN** its generated C++ callback receives mutable Transform access

#### Scenario: Project entry exposes only frame-local output
- **WHEN** an external handler lists Highlight under projects but not writes
- **THEN** its generated callback can project Highlight to a live target but cannot durably mutate or attach Highlight through that capability

### Requirement: external projected outputs participate in producer scheduling
An external `projects:` entry SHALL create the same project-producer scheduling information as an inferred regular `project` statement. For the same activation, a projected producer SHALL be ordered before handlers that match or read the projected trait according to the ordinary handler-conflict rules.

#### Scenario: External projection precedes reader
- **WHEN** an external input handler projects PointerHit and a later input handler reads PointerHit
- **THEN** the execution graph orders the producer before the reader without classifying PointerHit as a durable write

### Requirement: Selection does not imply trait access
`filter:` and `exclude:` SHALL determine entity selection without adding reads or writes. An `order by:` expression SHALL add a read for every trait whose data it evaluates.

#### Scenario: Filter-only trait is not read
- **WHEN** a handler filters Disabled as a marker and never accesses its fields
- **THEN** Disabled is absent from the handler's reads and writes

#### Scenario: Sort key adds read
- **WHEN** a rule orders by `transform.position.x`
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
