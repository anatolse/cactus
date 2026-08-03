# handler-execution-graph Specification

## Purpose
Define canonical handler execution-graph nodes and the deterministic dependency, event-flow, phase, conflict, and commit edges backends use to schedule handlers.

## Requirements

### Requirement: Canonical handler graph nodes
The compiler SHALL produce one execution-graph node per handler with canonical identity composed from module, owning rule, and resolved trigger. A rule MUST NOT declare more than one handler for the same trigger.

#### Scenario: Separate rule handlers become separate nodes
- **WHEN** Player declares `on fixed_tick` and `on Damaged`
- **THEN** the graph contains distinct Player.fixed_tick and Player.Damaged nodes

#### Scenario: Duplicate trigger is rejected
- **WHEN** one rule declares two handlers for fixed_tick
- **THEN** semantic analysis reports a duplicate handler trigger

### Requirement: Relation cardinality does not multiply graph nodes
A selectionless, unary, or pair handler SHALL each occupy one canonical execution-graph node. Runtime entities and pair tuples SHALL execute within that node and MUST NOT alter canonical identity or create dynamic graph nodes.

#### Scenario: Pair tuple count is graph-independent
- **WHEN** a pair handler's snapshots produce one thousand tuples
- **THEN** the graph contains one node for that rule and trigger

### Requirement: Phase and event flow edges
The graph SHALL include phase barrier edges from upstream phase completion to downstream phase handlers and event-flow edges from a handler that may emit an event to every handler consuming that event. Event-handler nodes SHALL be activated per event occurrence within the current activation's bounded cascade.

#### Scenario: Event producer activates consumers
- **WHEN** NativeMovement declares `emits: EntityMoved` and Player handles EntityMoved
- **THEN** the graph records an event-flow edge from NativeMovement.fixed_tick to Player.EntityMoved

#### Scenario: Fixed barrier covers all repetitions
- **WHEN** tick follows fixed_tick
- **THEN** tick handler nodes depend on completion of the entire fixed_tick repetition batch

### Requirement: Event-flow edges are recipient-independent
The graph SHALL connect an event producer to every consumer of that event regardless of whether a runtime occurrence is broadcast or targeted. Recipient-aware relation restriction SHALL occur during delivery without changing the static event-flow graph.

#### Scenario: Targeted occurrence uses existing event edge
- **WHEN** DetectContacts may emit Contact to a body and ResolveContact consumes Contact
- **THEN** one static event-flow edge connects the handlers and runtime routing selects the recipient

### Requirement: Commit-synthesized and scheduler-boundary producer edges
Beyond handler-to-handler `EventFlowEdge`s, the execution graph SHALL represent two further event-producer kinds so every consumer node's trigger resolves to a declared producer:

- **Commit-synthesized producer**: an event produced by the activation commit step itself as a direct consequence of applying a buffered structural command (`std.core.spawn` for an applied `Spawn`; `std.core.destroy` for an applied `Destroy`). The producer is the commit step, not any `HandlerIdentity`.
- **Scheduler-boundary producer**: an event injected exactly once by the scheduler at a fixed program boundary rather than per real-frame or per-command (`std.core.load` at the boot boundary, after initial entities exist and before the first frame occurrence; `std.core.unload` at the teardown boundary, after the last frame occurrence and before shutdown).

These are distinct from the existing host-injected extern-event mechanism (`pub extern event`, e.g. `frame`), which is injected by the embedding application rather than synthesized by the graph's own commit/scheduler machinery.

#### Scenario: Commit-synthesized edge for spawn
- **WHEN** a rule declares `on spawn` and no handler in the program declares `emits: spawn`
- **THEN** the graph still records a producer edge into that handler's node, attributed to the commit step rather than to any `HandlerIdentity`

#### Scenario: Scheduler-boundary edge for load
- **WHEN** a rule declares `on load`
- **THEN** the graph records a producer edge into that handler's node attributed to the scheduler's boot boundary, distinct from an `EventFlowEdge` whose producer is another handler

#### Scenario: Handler-emitted producer edges are unaffected
- **WHEN** a handler declares `emits: EntityMoved` and another handler consumes `EntityMoved`
- **THEN** the graph continues to record this as the existing handler-to-handler `EventFlowEdge`, unaffected by the new producer kinds

#### Scenario: A consumed trigger with no producer of any kind is a distinguishable graph state
- **WHEN** a handler's trigger is neither `frame`-like extern, commit-synthesized, scheduler-boundary, nor the target of any handler's `emits`
- **THEN** the graph can identify that the trigger has zero producer edges of any kind (this requirement defines the representation only; a validation pass that rejects this state is not part of this change)

### Requirement: Contract conflict edges
Handlers eligible in the same activation SHALL be serialized when one writes a trait the other reads or writes, or when they share an observable effect domain. Filters alone SHALL NOT create conflict edges.

Pair direction SHALL be chosen by explicit handler ordering first; otherwise a one-way writer-to-reader dependency SHALL run writer first; reciprocal or write/write/effect conflicts SHALL use stable declaration order. The tie-break SHALL be deterministic across builds.

#### Scenario: Writer precedes reader
- **WHEN** one fixed_tick handler writes Transform and another reads Transform with no reverse hazard or explicit order
- **THEN** the graph orders the writer before the reader

#### Scenario: Independent reads can share a graph level
- **WHEN** two handlers only read disjoint or identical traits and have no shared effect
- **THEN** no conflict edge is added between them

#### Scenario: Shared graphics effects are deterministic
- **WHEN** two render handlers declare `effects: graphics` without explicit ordering
- **THEN** their conflict is oriented by stable declaration order

### Requirement: Pair contracts participate in conservative conflicts
Graph conflict construction SHALL use the conservative trait reads, durable writes, projected outputs, commands, and effects inferred for pair handlers. Binding-qualified reads SHALL be retained as provenance but SHALL NOT weaken correctness by removing conservative conflicts.

#### Scenario: Pair reader follows unary writer
- **WHEN** a unary handler writes Transform and a pair handler reads Transform on either binding in the same activation
- **THEN** the graph adds the same writer-before-reader dependency required for unary consumers

### Requirement: Explicit handler ordering
A handler SHALL accept an optional leading `after:` block naming canonical or qualified handler nodes. The referenced node MUST be eligible under the same trigger or activation context. Existing rule-level `after:` SHALL remain compatibility shorthand that creates edges only between matching triggers on the referenced and dependent rules.

#### Scenario: Handler-level edge resolves
- **WHEN** `SpriteRenderer.render` declares after `TransformInterpolation.render`
- **THEN** the graph records that explicit handler edge

#### Scenario: Rule shorthand applies to matching phase
- **WHEN** rule B is after rule A and both have tick and Damaged handlers
- **THEN** B.tick follows A.tick and B.Damaged follows A.Damaged without adding cross-trigger edges

### Requirement: Graph validation and deterministic execution
The compiler SHALL reject cycles in the phase graph and in each handler scheduling graph after combining explicit ordering and contract-conflict edges, and SHALL report a handler-level cycle path. Event-flow edges SHALL be validated separately and MAY be cyclic because delivery is controlled by the bounded event-cascade rule. A sequential backend SHALL use a stable topological order; a parallel backend MAY execute nodes in the same dependency level concurrently while preserving activation commit semantics.

#### Scenario: Handler cycle is diagnosed
- **WHEN** combined ordering creates A.tick -> B.tick -> A.tick
- **THEN** semantic analysis reports the canonical handler cycle

#### Scenario: Event feedback is not a scheduling cycle
- **WHEN** an A consumer may emit B and a B consumer may emit A
- **THEN** semantic analysis accepts the event-flow cycle and runtime cascade limits govern delivery

#### Scenario: Independent nodes remain parallelizable
- **WHEN** two nodes have no dependency or conflict edge
- **THEN** the graph preserves them in the same executable dependency level
