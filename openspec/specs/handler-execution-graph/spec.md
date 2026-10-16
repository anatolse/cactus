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
The graph SHALL include phase barrier edges from upstream phase completion to downstream phase handlers and event-flow edges from a handler that may emit an event to every handler consuming that event. Event-handler nodes SHALL be activated per event occurrence within the current activation along immediate edges, and in a later activation along delayed edges, as defined by "Every event cycle passes through a delay".

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
Every execution-graph event producer SHALL carry a typed producer kind rather than an implied handler identity. Beyond handler-to-handler production, the graph SHALL represent two further event-producer kinds so every consumer node's trigger resolves to a declared producer:

- **Commit-synthesized producer**: a trigger delivered by the activation commit step itself as a direct consequence of applying buffered structural commands. This covers trait lifecycle triggers: `on added T` is produced by commits that apply a `Spawn` of a template carrying `T` or an `Add` of `T`; `on removed T` by commits that apply a `Remove` of `T`. The producer is the commit step, not any `HandlerIdentity`.
- **Scheduler-boundary producer**: an event injected exactly once by the scheduler at a fixed program boundary rather than per real-frame or per-command (`std.core.load` at the boot boundary, after initial entities exist and before the first frame occurrence; `std.core.unload` at the teardown boundary, after the last frame occurrence and before shutdown). The startup arrival activation's `on added` deliveries for load-time entities are also a scheduler-boundary production.

Producer kind SHALL be determined only from resolved canonical identities and contract metadata the graph already carries — the consumer's canonical trigger identity and kind, and the structural commands recorded on handler contracts. Rule names, import aliases, and source spelling SHALL NOT determine producer kind.

Host-injected external events (`pub extern event`, e.g. `frame`) originate outside the graph's own commit and scheduler machinery. They remain distinct in origin from both kinds above and are represented as their own producer kind rather than folded into either.

#### Scenario: Commit-synthesized edge for spawn
- **WHEN** a rule declares `on added Dying` and another handler's contract records `add Dying`
- **THEN** the graph records a producer edge into the `on added Dying` handler's node, attributed to the commit step rather than to any `HandlerIdentity`

#### Scenario: Scheduler-boundary edge for load
- **WHEN** a rule declares `on load`
- **THEN** the graph records a producer edge into that handler's node attributed to the scheduler's boot boundary, distinct from an `EventFlowEdge` whose producer is another handler

#### Scenario: Handler-emitted producer edges are unaffected
- **WHEN** a handler declares `emits: EntityMoved` and another handler consumes `EntityMoved`
- **THEN** the graph continues to record this as the existing handler-to-handler `EventFlowEdge`, unaffected by the new producer kinds

#### Scenario: Producer kind is recoverable without re-deriving it
- **WHEN** a consumer node's incoming producer edges are inspected
- **THEN** each edge reports its producer kind directly, so a consumer activated by the commit step is distinguishable from a consumer activated by a handler emitting the same event

#### Scenario: A consumed trigger with no producer of any kind is a distinguishable graph state
- **WHEN** a handler's trigger is neither `frame`-like extern, commit-synthesized, scheduler-boundary, nor the target of any handler's `emits`
- **THEN** the graph can identify that the trigger has zero producer edges of any kind

### Requirement: Host external-event source producers
The graph SHALL represent a host external-event source producer for every consumed event declared `pub extern`, attributing the event's injection to the embedding application rather than to any handler, commit step, or scheduler boundary. An event that is not declared external SHALL NOT receive a host external-event source producer.

#### Scenario: Extern event consumer resolves to a host source
- **WHEN** a rule declares `on frame` and `frame` is declared `pub extern event`
- **THEN** the graph records a producer edge into that handler's node attributed to a host external source, distinct from a handler-emitted edge

#### Scenario: Non-extern event gets no host source
- **WHEN** a consumed event is declared without `extern`
- **THEN** the graph attributes no host external-event source producer to it

#### Scenario: External and handler production coexist
- **WHEN** an event is declared `pub extern` and a handler also declares `emits:` for it
- **THEN** the graph records both the host external-source producer edge and the handler-emitted edge into each consumer

### Requirement: Producer records do not alter scheduling
Event-producer records SHALL be additive graph data. Introducing or reading them SHALL NOT change per-activation schedule dependencies, stable topological order, dependency levels, phase barriers, or any reported diagnostic. Producer relations describe event activation and MAY participate in cycles; they SHALL NOT participate in schedule-cycle validation. A producer relation inside a cycle is delayed as defined by "Every event cycle passes through a delay".

#### Scenario: Schedule is unchanged by producer representation
- **WHEN** the same program's execution graph is compared with and without producer records present
- **THEN** its schedule edges, stable topological order, and dependency levels are identical

#### Scenario: Producer cycles remain legal
- **WHEN** an `on added Minion` handler emits an event whose consumer queues a further `spawn` of a template carrying `Minion`
- **THEN** the resulting producer and event-flow cycle is legal, its edges are delayed, and no schedule cycle is reported

### Requirement: Pair contracts participate in conservative conflicts
Graph conflict construction SHALL use the conservative trait-field reads, durable writes, projected outputs, commands, and effects inferred for pair handlers. Binding-qualified reads SHALL be retained as provenance but SHALL NOT weaken correctness by removing conservative conflicts.

#### Scenario: Pair reader follows unary writer
- **WHEN** a unary handler writes Transform and a pair handler reads Transform on either binding in the same activation
- **THEN** the graph adds the same writer-before-reader dependency required for unary consumers

#### Scenario: Pair reader of another field does not conflict
- **WHEN** a unary handler writes only `Collider.mask` and a pair handler reads only `Collider.radius` in the same activation
- **THEN** no data conflict edge is added between them

### Requirement: Explicit handler ordering
A handler SHALL accept an optional leading `after:` block naming canonical or qualified handler nodes. The referenced node MUST be eligible under the same trigger or activation context. Existing rule-level `after:` SHALL remain compatibility shorthand that creates edges only between matching triggers on the referenced and dependent rules.

#### Scenario: Handler-level edge resolves
- **WHEN** `SpriteRenderer.render` declares after `TransformInterpolation.render`
- **THEN** the graph records that explicit handler edge

#### Scenario: Rule shorthand applies to matching phase
- **WHEN** rule B is after rule A and both have tick and Damaged handlers
- **THEN** B.tick follows A.tick and B.Damaged follows A.Damaged without adding cross-trigger edges

### Requirement: Graph validation and deterministic execution
The compiler SHALL reject cycles in the phase graph and in each handler scheduling graph after combining explicit ordering and contract-conflict edges, and SHALL report a handler-level cycle path. Event-flow and lifecycle edges SHALL be validated separately and MAY be cyclic, because every such cycle passes through a delayed edge. A sequential backend SHALL use a stable topological order; a parallel backend MAY execute nodes in the same dependency level concurrently while preserving activation commit semantics.

#### Scenario: Handler cycle is diagnosed
- **WHEN** combined ordering creates A.tick -> B.tick -> A.tick
- **THEN** semantic analysis reports the canonical handler cycle

#### Scenario: Event feedback is not a scheduling cycle
- **WHEN** an A consumer may emit B and a B consumer may emit A
- **THEN** semantic analysis accepts the event-flow cycle, and both edges of the cycle are delayed

#### Scenario: Independent nodes remain parallelizable
- **WHEN** two nodes have no dependency or conflict edge
- **THEN** the graph preserves them in the same executable dependency level

### Requirement: Render-pass synthetic pass-local edges
For a render-pass phase, the execution graph SHALL represent one synthetic, non-authored rasterization node connecting the phase's vertex-stage handler node to its fragment-stage handler node: vertex-handler node → synthetic rasterization node → fragment-handler node → the render-pass phase's own node in the ordinary phase DAG. This mirrors the existing representation of non-authored producer work ("Commit-synthesized and scheduler-boundary producer edges") — the synthetic node is attributed to backend rasterization, not to any `HandlerIdentity`. No other phase or handler may depend on the synthetic node directly; only the phase's own node participates in ordinary `after:` edges to downstream phases.

#### Scenario: Vertex output reaches fragment through the synthetic node
- **WHEN** a render-pass phase has vertex-stage handler `V` and fragment-stage handler `F`
- **THEN** the graph contains edges `V → synthetic rasterization node → F`, and no direct edge `V → F`

#### Scenario: Downstream phases depend on the pass, not on either stage handler directly
- **WHEN** a phase declares `after: particle_pass`
- **THEN** its producer edge is to `particle_pass`'s own phase node, unaffected by the internal vertex/rasterization/fragment edges

#### Scenario: Stage handlers introduce no new structural-commit interaction
- **WHEN** the graph is validated for a render-pass phase
- **THEN** no structural-command producer/consumer edges are introduced by the stage handlers, since `dsl-render-passes`'s body restriction already rejects `spawn`/`destroy`/`add`/`remove`/`project` in a stage handler body

### Requirement: An activation denotes a trace
The meaning of one activation SHALL be defined independently of any backend's handler order. Each handler run is a step. Two handlers *commute* when neither writes a trait field the other reads or writes, they share no observable effect domain, and they do not both append to one event queue or one structural command list in an order that a later reader can observe; otherwise they *depend*. For every pair of co-eligible handlers that depend, the graph SHALL hold a schedule edge between them.

The result of an activation SHALL be the result of running its handlers in any order that respects the schedule edges (a linear extension). Every such order SHALL give the same world, the same queued events and commands, and the same effect calls. This holds for each set of handlers dispatched together: the phase handlers of an activation, the consumers of one event occurrence, and the lifecycle consumers of one commit round. The stable topological order is one linear extension; a backend SHALL NOT rely on any property of it that other linear extensions lack.

#### Scenario: Commuting handlers give one result in either order
- **WHEN** two `tick` handlers write disjoint traits and read nothing the other writes
- **THEN** the graph holds no edge between them, and running either first gives the same world

#### Scenario: Every linear extension gives the same world
- **WHEN** an activation's handlers admit several orders that respect its schedule edges
- **THEN** running the activation in each of those orders, from the same initial world, gives identical worlds, queued events and commands

#### Scenario: The stable order is not special
- **WHEN** a backend runs an activation in a linear extension other than the stable topological order
- **THEN** the result is the same as with the stable topological order

### Requirement: Dependency levels are the canonical form of the trace
For each activation, dependency level 0 SHALL contain the handlers with no incoming schedule edge, and level k SHALL contain the handlers whose longest incoming path of schedule edges has length k. Every schedule edge SHALL go from a lower level to a higher one, so no two handlers in one level depend. The levels SHALL be determined by the handlers and edges alone, not by declaration order; declaration order MAY only order handlers inside one level for display and for the stable topological order. Running the handlers of one level concurrently, then the next level, SHALL be a correct execution by definition.

#### Scenario: Levels follow the longest path
- **WHEN** an activation has edges A → B and B → C, and handler D has no edges
- **THEN** A and D are in level 0, B is in level 1 and C is in level 2

#### Scenario: Handlers in one level do not depend
- **WHEN** two handlers are in the same dependency level
- **THEN** the graph holds no schedule edge between them in either direction

#### Scenario: Declaration order does not change levels
- **WHEN** the declarations of two rules in one activation are swapped in the source
- **THEN** every handler keeps its dependency level

### Requirement: Every schedule edge carries one reason
Every schedule edge SHALL record exactly one reason for its direction, from this closed set:

- **explicit**: an `after:`, `before:` or group ordering the author wrote;
- **writer before reader**: only one side writes what the other reads;
- **derivation after writers**: a derived trait's computation runs after the handlers that write its inputs;
- **importer before public group**: the direction rule for a handler against a `pub group` of a module it imports;
- **declaration order**: none of the above applies, and stable linked declaration order picked the direction.

The first four reasons SHALL be *meaningful*: they follow from what the definition says. Declaration order SHALL NOT be a meaningful reason: an edge whose reason is declaration order marks a pair of handlers whose order the definition does not determine. The graph, the module artifact, and the linked program SHALL preserve each edge's reason, so the edges ordered only by declaration order can be listed without re-deriving them.

#### Scenario: Explicit order is recorded as explicit
- **WHEN** rule B declares `after: A` and their `tick` handlers conflict
- **THEN** the edge from `A.tick` to `B.tick` records the reason explicit

#### Scenario: One-way data flow is recorded
- **WHEN** one `tick` handler writes `Transform.position` and another only reads it, with no explicit order
- **THEN** the edge records the reason writer before reader

#### Scenario: A declaration-order edge is distinguishable
- **WHEN** two `tick` handlers both assign `Score.value` and nothing else orders them
- **THEN** their edge records the reason declaration order, and listing the program's declaration-order edges includes it

#### Scenario: Reasons survive linking
- **WHEN** a module is compiled to an artifact and linked into a program
- **THEN** each of its schedule edges has the same reason in the linked program as in the module's own graph

### Requirement: Contract conflict edges are oriented only by meaningful rules
Handlers eligible in the same activation SHALL be serialized when they conflict. They conflict on data when one writes a trait field the other reads or writes, unless the two writes commute (see "Commuting writes add no edge"). They conflict on a channel as defined in "Channel conflicts". Two handlers conflict on a trait only when their field accesses to it overlap; an access that covers all fields of a trait overlaps every field of it. A projected output SHALL keep producing a conflict with a handler that selects the projected trait, as before. Filters alone SHALL NOT create conflict edges. A data conflict edge SHALL name each overlapping trait and, for each, the overlapping fields or that the overlap covers all fields.

Pair direction SHALL be chosen only by a meaningful orientation, in this order:

1. **Derivation after writers**: a derivation handler runs after the handlers that write what it derives from.
2. **Explicit order**: handler, rule or group `after:`/`before:` edges, directly or transitively.
3. **Importer before public group**: when exactly one handler of the pair is a member of a `pub group` declared in module M and the other handler's module imports M directly or transitively, the direction SHALL be decided against the whole group, so every conflict between that handler and the group's members points the same way: when the handler and the group's members together conflict in both directions, or only through shared effects or channels, the importing module's handler SHALL run first; when only one side writes what the other reads, the writer SHALL run first.
4. **Writer before reader**: when only one handler of the pair writes what the other reads, the writer SHALL run first.

Declaration order, link order and source position SHALL NOT orient a conflict. A conflict that none of these orients SHALL be rejected as described in "Unorientable conflicts are compile errors".

#### Scenario: Writer precedes reader
- **WHEN** one fixed_tick handler writes Transform and another reads Transform with no reverse hazard or explicit order
- **THEN** the graph orders the writer before the reader

#### Scenario: Importing module runs before another module's public group
- **WHEN** a game rule writes `CharacterBody.velocity` in `fixed_tick`, a member of `std.physics.volume`'s `pub group solve` reads and writes `CharacterBody.velocity`, and neither declares an order against the other
- **THEN** the graph orders the game rule's handler before the group member

#### Scenario: The whole group decides the direction
- **WHEN** a game rule writes `CharacterBody.velocity` and reads `WorldTransform.position`, one `solve` member only writes `WorldTransform.position`, and another reads and writes `CharacterBody.velocity`
- **THEN** the graph orders the game rule's handler before both members, with no cycle

#### Scenario: Explicit order overrides the group rule
- **WHEN** the same game rule declares `after: physics.solve`
- **THEN** the graph orders every group member before the game rule's handler

#### Scenario: One-way reader still follows the writer
- **WHEN** a game rule only reads `CharacterBody.grounded` in `fixed_tick` and a member of `solve` writes it
- **THEN** the graph orders the group member before the game rule's handler

#### Scenario: Same-module reciprocal conflict needs an explicit order
- **WHEN** two rules of one module conflict reciprocally, one is a member of that module's `pub group`, and neither declares an order against the other
- **THEN** compilation fails with an unorientable-conflict error naming both handlers

#### Scenario: Independent reads can share a graph level
- **WHEN** two handlers only read disjoint or identical traits and act on no effect domain the other uses
- **THEN** no conflict edge is added between them

#### Scenario: Shared graphics effects need an explicit order
- **WHEN** two render handlers declare `effects: graphics` and nothing orders them
- **THEN** compilation fails with an unorientable-conflict error naming both handlers and the `graphics` domain

#### Scenario: Shared graphics effects follow explicit order
- **WHEN** two render handlers declare `effects: graphics` and one declares `after:` naming the other
- **THEN** the graph records an effect conflict edge in that direction

#### Scenario: Disjoint fields of one trait do not conflict
- **WHEN** in the same activation one handler writes `Match.over`, a second writes `Match.score`, and a third reads `Match.over`
- **THEN** the graph adds exactly one data conflict edge, from the `Match.over` writer to the `Match.over` reader

#### Scenario: Whole-trait access conflicts with every field
- **WHEN** one handler writes `Match.score` and an `extern rule` handler in the same activation lists `Match` under `reads:`
- **THEN** the graph orders the writer before the extern reader

#### Scenario: Edge names overlapping fields
- **WHEN** a handler writes `Match.over` and `Match.score` and another reads only `Match.over`
- **THEN** the conflict edge names `Match` with the field `over` only

#### Scenario: Moving a rule to another file does not change the schedule
- **WHEN** a program compiles, and one of its rules is moved to another module or its imports are reordered without changing any contract or explicit order
- **THEN** the program still compiles and every schedule edge keeps its direction

### Requirement: Commuting writes add no edge
Two handlers' writes to the same trait field SHALL commute when both have kind Accum, or both have kind Join with the same operator. A field on which the two handlers' only accesses are commuting writes SHALL NOT contribute to a data conflict between them. Every other overlap on that field, and every overlap on other fields, SHALL conflict as before.

#### Scenario: Two damage sources share a level
- **WHEN** in one activation handler A runs `h.hp -= burn` and handler B runs `h.hp -= poison`, both Accum, and they share no other conflicting access
- **THEN** no schedule edge connects A and B

#### Scenario: Same Join operator commutes
- **WHEN** handler A runs `actor.grounded = true` and handler B runs `actor.grounded = actor.grounded or touching`
- **THEN** no schedule edge on `Actor.grounded` connects A and B

#### Scenario: Different Join operators conflict
- **WHEN** handler A runs `actor.grounded = false` and handler B runs `actor.grounded = true`, and nothing orders them
- **THEN** compilation fails with an unorientable-conflict error naming `Actor.grounded`, with kinds Join `and` and Join `or`

#### Scenario: Accum against Assign conflicts
- **WHEN** handler A runs `t.x += 1.0` and handler B runs `t.x = 0.0`, and nothing orders them
- **THEN** compilation fails with an unorientable-conflict error naming `T.x`, with kinds Accum and Assign

#### Scenario: Accum writer precedes a reader
- **WHEN** handler A runs `h.hp -= 5` and handler B only reads `h.hp`
- **THEN** the graph orders A before B as writer before reader

#### Scenario: A delta that reads the other's write still conflicts
- **WHEN** handler A runs `p.x += p.v` and handler B runs `p.v = 0.0` and `p.x += 1.0`
- **THEN** `P.x` adds no conflict, and the overlap on `P.v` orders B before A as writer before reader

### Requirement: Channel conflicts
Handlers eligible in the same activation SHALL also conflict through shared channels, using the same orientation rules as data conflicts and recording an edge that names the channel:

- **Commands**: per canonical trait, `add` of a marker trait (a trait with no fields) commutes with another `add` of it, and `remove` commutes with `remove`. `destroy` commutes with every command. `spawn` commutes with every command. `add` of a trait with fields conflicts with another `add` of that trait. `add` and `remove` of one trait conflict. `set` of a trait conflicts with `add` or `remove` of that trait, and with another `set` of it when their patched fields overlap.
- **Occurrences**: two handlers P1 and P2 conflict when there is a position (see "Occurrences are delivered in generations"), counted from the position at which P1 and P2 run, where a run reached from P1 and a run reached from P2 do not commute under the data, command and effect rules here. A run is *reached* from P through emitted events, through lifecycle triggers delivered by commands P issues, and transitively through the runs those reach. Two runs of one handler count as two runs. Runs reached through a delayed edge SHALL be treated as sharing one position with every other run reached through a delayed edge from P1 or P2, whatever activation they land in. Runs reached from one handler alone never conflict through this rule, because their order is that handler's production order.
- **Effects**: two handlers that carry the same effect domain conflict when at least one of them acts on it. Two handlers that only observe a domain do not conflict through it.

#### Scenario: Marker adds commute
- **WHEN** two unordered handlers both `add Stunned` and `Stunned` has no fields
- **THEN** no edge connects them through that command

#### Scenario: Add against remove conflicts
- **WHEN** one handler runs `add Stunned` and another `remove Stunned`, and nothing orders them
- **THEN** compilation fails with an unorientable-conflict error naming the `Stunned` commands

#### Scenario: Destroy commutes
- **WHEN** one handler runs `destroy` and another runs `set Health on target:`
- **THEN** no edge connects them through commands

#### Scenario: Disjoint set patches commute
- **WHEN** one handler's `set Health` patches only `hp` and another's patches only `max_hp`
- **THEN** no edge connects them through commands

#### Scenario: Order-insensitive event consumer allows unordered producers
- **WHEN** handlers A and B both emit `Damaged` and the only consumer of `Damaged` runs `h.hp -= amount`
- **THEN** no edge connects A and B through `Damaged`

#### Scenario: Order-sensitive event consumer needs ordered producers
- **WHEN** handlers A and B both emit `Spoke` with no order between them and the consumer of `Spoke` runs `log.last = line`
- **THEN** compilation fails with an unorientable-conflict error naming A, B, `Spoke` and the consumer

#### Scenario: Different events with conflicting consumers need ordered producers
- **WHEN** unordered handlers A and B emit `Hit` and `Heal` to the same entity, the `Hit` handler runs `h.hp = 0` and the `Heal` handler runs `h.hp = 10`
- **THEN** compilation fails with an unorientable-conflict error naming A, B, `Hit`, `Heal` and both consumers

#### Scenario: Consumers at different generations do not race
- **WHEN** unordered handlers A and B emit `E1` and `E2`, the `E1` handler emits `E3`, the `E2` handler runs `h.hp = 10` and the `E3` handler runs `h.hp = 0`
- **THEN** no edge connects A and B through these events, and the program compiles

#### Scenario: Different lifecycle triggers with conflicting consumers need ordered producers
- **WHEN** unordered handlers A and B run `add Burning` and `add Wet` on the same entity, the `on added Burning` handler runs `t.temp = 100.0` and the `on added Wet` handler runs `t.temp = 0.0`
- **THEN** compilation fails with an unorientable-conflict error naming A, B, both triggers and both consumers

#### Scenario: One producer's emissions never race with each other
- **WHEN** one handler emits `Hit` and then `Heal`, the `Hit` handler runs `h.hp = 0` and the `Heal` handler runs `h.hp = 10`
- **THEN** the program compiles and the `Heal` handler's write is the final value

#### Scenario: A same-event chain still conflicts at its deeper generation
- **WHEN** unordered handlers A and B both emit `Ping`, the `Ping` handler only emits `Pong`, and the `Pong` handler runs `log.last = line`
- **THEN** compilation fails with an unorientable-conflict error naming A, B, the `Pong` handler and the path through `Ping` and `Pong`

#### Scenario: Observing effect calls add no edge
- **WHEN** two handlers of one activation both only call `input.pressed`, and nothing else conflicts
- **THEN** no edge connects them

#### Scenario: Observing and acting conflict
- **WHEN** one handler calls `input.pressed(Fire)`, another calls `input.consume(Fire)`, and nothing orders them
- **THEN** compilation fails with an unorientable-conflict error naming both handlers and the `input` domain

### Requirement: Unorientable conflicts are compile errors
When two handlers conflict and no meaningful orientation from "Contract conflict edges are oriented only by meaningful rules" orders them, compilation SHALL fail. The diagnostic SHALL be located at one of the two handlers and SHALL name both canonical handlers; for a data conflict, each overlapping field with both handlers' write kinds (or "read"); for a command or effect conflict, the commands or effect domain; for an occurrence conflict, the two reached runs that do not commute, and the events or lifecycle triggers through which each producer reaches its run. It SHALL suggest adding `after:` naming either handler to the other. The compiler SHALL report every unorientable conflict in the program, not only the first. A conflict that crosses modules SHALL be reported when the program is linked.

#### Scenario: Diagnostic names both sides and both fixes
- **WHEN** `Regen/on tick` assigns `h.hp = h.max_hp` and `Poison/on tick` runs `h.hp -= 1`, and nothing orders them
- **THEN** the error names `Regen/on tick` and `Poison/on tick`, `Health.hp` with kinds Assign and Accum, and suggests `after: Regen/on tick` in Poison or `after: Poison/on tick` in Regen

#### Scenario: All ambiguities are reported
- **WHEN** a program has three unrelated unorientable conflicts
- **THEN** compilation reports three errors

#### Scenario: Cross-module ambiguity is a link error
- **WHEN** two modules that don't import each other each have a rule that assigns `Score.value` in `tick`
- **THEN** linking the program fails with an unorientable-conflict error naming both handlers

#### Scenario: Occurrence conflict names the path to each run
- **WHEN** unordered `A/on tick` and `B/on tick` emit `Hit` and `Heal`, whose handlers both assign `Health.hp`
- **THEN** the error names `A/on tick` reaching the `Hit` handler through `Hit`, `B/on tick` reaching the `Heal` handler through `Heal`, `Health.hp` with kinds Assign and Assign, and suggests `after:` between A and B

### Requirement: A trigger with no producer is a compile error
After linking, the compiler SHALL report an error for every handler in a non-stdlib module whose trigger has no producer of any kind. Producers SHALL be counted only from reachable handlers and reachable archetypes: an `emits:` in an unreachable handler produces nothing, and `on added T` / `on removed T` has a commit-synthesized producer only when a reachable archetype can gain or lose `T`. The diagnostic SHALL name the handler and its trigger. A stdlib handler with no producer SHALL NOT be an error; it is recorded as unreachable, like a stdlib rule whose domain matches nothing.

#### Scenario: Event nobody emits
- **WHEN** a user rule declares `on Exploded` and no reachable handler emits `Exploded` and `Exploded` is not `extern`
- **THEN** compilation fails with an error naming the handler and `Exploded`

#### Scenario: Lifecycle trigger on a trait nothing gains
- **WHEN** a user rule declares `on added Dying` and no reachable archetype can gain `Dying`
- **THEN** compilation fails with an error naming the handler and `added Dying`

#### Scenario: Extern event always has a producer
- **WHEN** a user rule declares `on frame` and `frame` is `pub extern event`
- **THEN** no missing-producer error is reported

#### Scenario: Emitter that is itself dead does not count
- **WHEN** the only handler emitting `Hit` is a stdlib handler recorded as unreachable, and a user rule declares `on Hit`
- **THEN** compilation fails with a missing-producer error for the user handler

#### Scenario: Unproduced stdlib trigger is not an error
- **WHEN** a stdlib rule declares `on SaveRequested` and the program never emits `SaveRequested`
- **THEN** compilation succeeds and the stdlib handler is recorded as unreachable

### Requirement: Every event cycle passes through a delay
The compiler SHALL build a delivery graph over the linked program's handler nodes with two kinds of edges:

- an event-flow edge from each handler that may emit an event to each handler consuming that event;
- a lifecycle edge from each handler whose contract may add trait `T` (an `add T`, a `spawn` whose template's composed trait set or own additions include `T`, a `keep` of `T`, or a state transition into variant `T`) to each `on added T` handler, and from each handler whose contract may remove `T` (a `remove T`, a `keep` of `T`, or a state transition out of variant `T`) to each `on removed T` handler.

The compiler SHALL compute the strongly connected components of this graph. A component is cyclic when it has more than one handler or its single handler has an edge to itself. An edge whose producer and consumer are in the same cyclic component SHALL be a delayed edge; every other edge SHALL be an immediate edge. Each event-flow edge SHALL record whether it is delayed. The classification SHALL be computed on the whole linked program, so a cycle that spans modules is found, and SHALL depend only on resolved canonical identities, never on rule names, import aliases, source spelling, or declaration order.

#### Scenario: A self-emitting handler has a delayed self edge
- **WHEN** `Answer` handles `Knock` and emits `Knock`
- **THEN** the event-flow edge from `Answer.Knock` to `Answer.Knock` is delayed

#### Scenario: A two-handler event cycle delays both edges
- **WHEN** an `A` consumer may emit `B` and a `B` consumer may emit `A`
- **THEN** both event-flow edges between the two handlers are delayed

#### Scenario: Edges leaving a cycle stay immediate
- **WHEN** `Answer` emits `Knock`, which `Answer` and `Logger` both consume, and `Logger` emits nothing
- **THEN** the edge from `Answer.Knock` to `Answer.Knock` is delayed and the edge from `Answer.Knock` to `Logger.Knock` is immediate

#### Scenario: A lifecycle self-loop is delayed
- **WHEN** an `on added Offspring` handler spawns a template that carries `Offspring`
- **THEN** the lifecycle edge from that handler to itself is delayed

#### Scenario: A mixed event and lifecycle cycle is delayed
- **WHEN** an `on added Burning` handler emits `Ignite` and an `Ignite` handler issues `add Burning`
- **THEN** both the event-flow edge and the lifecycle edge of that cycle are delayed

#### Scenario: An acyclic program has no delayed edge
- **WHEN** no handler can reach itself through event-flow and lifecycle edges
- **THEN** every edge is immediate

#### Scenario: A cycle across modules is found after linking
- **WHEN** a handler in module `a` emits an event consumed by a handler in module `b` that emits an event consumed by the first handler
- **THEN** the linked program's graph marks both edges delayed

### Requirement: Activation cascade depth is static
After delayed edges are removed, the delivery graph is acyclic. For every activation root — each phase, each event that can start an activation of its own, the load and unload boundaries, and the startup arrival activation — the compiler SHALL record the longest chain of immediate edges that can run in one activation started by that root, counting the root's own handlers as depth 1. Handlers that can receive a delayed occurrence in an activation of that root SHALL count as starting handlers of it. The recorded depth SHALL be finite for every program and SHALL NOT depend on any configured bound.

#### Scenario: An acyclic chain records its length
- **WHEN** a `tick` handler emits `A`, an `A` handler emits `B`, and nothing else in the program emits or consumes events or triggers lifecycle handlers
- **THEN** the recorded cascade depth for the `tick` activation is 3

#### Scenario: A cycle adds no depth beyond one hop
- **WHEN** a `fixed_tick` handler emits `Knock` and the `Knock` handler emits `Knock` to itself
- **THEN** the recorded cascade depth for the `fixed_tick` activation is 2

### Requirement: Occurrences are delivered in generations
Within one activation, occurrences SHALL be delivered in generations. The handlers dispatched at the activation's start (phase handlers, the host event's handlers, or the arrival deliveries) form generation 0. Occurrences emitted by runs of generation d form generation d + 1. Every occurrence of generation d SHALL be delivered before any occurrence of generation d + 1. Within one generation, occurrences SHALL be delivered in the order they were produced. Delayed occurrences delivered at the start of an activation's cascade SHALL belong to generation 1 of that activation, ahead of the occurrences its generation 0 produces.

Each commit round SHALL start a new cascade: its lifecycle deliveries form generation 0 of that round and are ordered as "Deterministic delivery order" in `dsl-trait-lifecycle-triggers` defines. A run's *position* is the pair (commit round, generation), with the activation's own cascade as round 0. Positions are ordered lexicographically. A run at an earlier position SHALL complete before any run at a later position starts.

#### Scenario: Generations do not interleave
- **WHEN** a `tick` handler emits `A` and then `B`, and the `A` handler emits `C`
- **THEN** the `A` handler runs, then the `B` handler, then the `C` handler

#### Scenario: Emission order inside one handler is kept
- **WHEN** one handler emits `DrawDebugLine2D`, then `DrawDebugTriangle2D`, then `DrawDebugLine2D`
- **THEN** the three renderer runs happen in that order
