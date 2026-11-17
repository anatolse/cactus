## Purpose

Define the design philosophy and guiding principles behind the Cactus language: a genre-neutral notation for games and tools, its gameplay-core teaching profile, the error classes it rules out by construction, predictability and total-semantics goals, its ECS-centric model, the declarative/imperative boundary, and the author/backend responsibility split.
## Requirements
### Requirement: Cactus language identity
Cactus SHALL be defined as a declarative, data-oriented notation for interactive simulations that compiles to an engine backend. Games come first. UI applications and tools, such as a game editor, SHALL be expressible with the same primitives. This identity statement SHALL be the authoritative reference for evaluating proposed language features.

Cactus has two goals, and they divide the work:
1. **The language** makes definitions short, simple to read, and declarative first, with imperative handlers and pure functions where behavior needs them. It rules out common coding errors by construction, above all memory management, lifetime management and data-race errors.
2. **The backend** turns those definitions into the most efficient code it can derive from the whole program: its data, its dependency graph, and its timing relations.

When the two pull against each other, the language stays simple and the backend does the extra work. The notation is the primary product; a backend is one realization of it.

The primary authoring audience is a game designer, gameplay programmer, or tool author who wants to describe what exists, what reacts to what, how state changes, and how it unfolds over time, without writing engine plumbing, lifetime management, or null-guard boilerplate.

#### Scenario: Language identity governs feature evaluation
- **WHEN** a new language feature is proposed
- **THEN** the proposal MUST address whether the feature strengthens authoring as a genre-neutral primitive or whether it belongs in the generated backend or stdlib instead

#### Scenario: Efficiency work goes to the backend, not the author
- **WHEN** a feature could be made faster either by asking authors for extra declarations or by deeper backend analysis
- **THEN** the proposal chooses backend analysis

#### Scenario: A tool is in scope
- **WHEN** a proposal is motivated by a UI application or editor rather than by a game
- **THEN** it is evaluated as part of the language's mission, under the same criteria as a gameplay proposal

### Requirement: Concise definitions support mathematical reasoning
Cactus's central thesis SHALL be that a small, analyzable notation can make complex interactive systems easy to describe while supporting mathematical proofs of correctness and efficient implementation. The syntax and frontend SHALL expose data, relations, state transitions and timing clearly enough for the backend to derive optimizations without asking authors to describe engine machinery.

This thesis distinguishes three obligations:
1. **Language correctness:** establish the determinacy, totality, activation termination and non-vacuity properties defined below, with the scope and limits of each analysis stated explicitly.
2. **Model correctness:** support reasoning about whether a system satisfies its intended domain properties. Defined execution alone does not establish conservation, numerical stability or gameplay intent; those properties and their assumptions must be supplied by the model or its specification.
3. **Compilation and cost correctness:** preserve observable source behavior through lowering and optimization, and justify resource bounds under explicit workload and execution assumptions. A legal optimization and a measured speedup are different claims; neither alone proves a universal performance guarantee.

These are design obligations, not a claim that the current compiler has mechanically proved them. Specifications and implementation reports SHALL distinguish intended guarantees, implemented checks, tested behavior and proved properties. Native operations and external inputs SHALL have explicit assumptions wherever a proof relies on them.

`spec/activation_semantics.md` is an explanatory model of one activation for a stated fragment: its state, transition rules, scoped arguments for termination and order independence, and open findings. It defines no requirement.

Facts derivable from a definition SHALL remain compiler responsibilities. This thesis introduces no proof annotations, performance controls or new grammar. Any future author-facing verification facility requires its own language design.

#### Scenario: An improvement is evaluated for authoring value
- **WHEN** a language or frontend improvement is proposed
- **THEN** its rationale explains what it does, why it is needed, which syntax or semantics it changes, and whether it makes equivalent system definitions more concise or easier to understand
- **AND** any added author obligation is justified against what the compiler can derive

#### Scenario: An optimization has a stated justification
- **WHEN** an optimization is described as mathematically justified
- **THEN** its justification states the assumptions and observable behavior it preserves
- **AND** any claimed resource bound names its workload parameters and execution model

#### Scenario: Defined arithmetic does not prove a domain law
- **WHEN** a simulation uses total deterministic arithmetic
- **THEN** conservation or numerical stability is claimed only with a separate argument under stated assumptions

### Requirement: Simplicity is defined against a gameplay-core profile
The project's simplicity claims SHALL apply to a curated gameplay-core profile rather than to the union of every current, experimental, or deferred language idea.

The gameplay-core profile SHALL be small enough to teach as a coherent authoring model for action-game mechanics such as platformers and shooters. It SHALL remain the teaching profile and the yardstick for simplicity.

A tools profile SHALL cover UI applications and editors. It SHALL be built from the same primitives as the gameplay-core profile plus stdlib modules, and SHALL NOT introduce syntax that only tools may use.

#### Scenario: Simplicity claim refers to the gameplay core
- **WHEN** project documentation describes Cactus as simple or beginner-friendly
- **THEN** that claim refers to the curated gameplay-core surface rather than the full set of deferred or backend-facing ideas

#### Scenario: Core profile remains focused on teachable mechanics
- **WHEN** a new feature is evaluated for the main language story
- **THEN** it is assessed against whether it preserves a small, teachable gameplay-core model

#### Scenario: Tools profile adds no private syntax
- **WHEN** a feature is proposed so that a UI application or editor can be written in Cactus
- **THEN** it is a primitive that gameplay code may also use, or it is a stdlib module, and never syntax that only tool code may use

### Requirement: Predictability is a first-class design goal
The Cactus execution model SHALL be fully predictable. Authors SHALL be able to reason about when and how every statement executes without consulting backend implementation details.

The following timing guarantees SHALL be documented and honored by all backends:
1. **Phase order**: declared `from:` and `after:` dependencies define activation barriers; the standard graph is input -> fixed_tick* -> tick -> late_tick -> render.
2. **Handler order within an activation**: explicit handler ordering and inferred contract-conflict edges define dependencies. Handlers whose writes commute need no order. A conflict that no explicit order or inferred meaningful rule orients is a compile error; declaration order, link order and source position never order handlers.
3. **Event delivery**: emitted events and lifecycle triggers activate contracted consumer nodes within the current activation, except that a delivery from a handler to a consumer on the same event or lifecycle cycle is delayed to the next activation of the producing clock. No configured bound changes when an occurrence is delivered.
4. **Structural change timing**: `add`, `remove`, `spawn`, and `destroy` commit after the complete handler/event cascade of each activation or fixed-step repetition.
5. **Fixed-step timing**: periodic cadence, catch-up cap, dropped excess time, fixed `dt`, and interpolation `alpha` follow phase declarations identically on every backend.
6. **External effects**: an action on an effect domain is serialized by graph order against every other use of that domain; observations of a domain that no co-eligible handler acts on may run in any order.
7. **`order by:` timing**: sorting occurs at a defined point before selected handler iteration and contributes trait reads to that handler contract.
8. **Iteration order within a pass**: a rule pass without `order by:` iterates in entity creation order wherever its result can observe the order; elsewhere the order is not observable, so it is free for the backend.

#### Scenario: Phase barrier is invariant
- **WHEN** fixed_tick repeats during a frame
- **THEN** tick begins only after all fixed repetitions and their commits

#### Scenario: Contract order is invariant
- **WHEN** one handler writes a trait consumed by another in the same activation
- **THEN** every backend honors the corresponding handler graph dependency

#### Scenario: Structural visibility is invariant
- **WHEN** a handler queues a structural command
- **THEN** no handler in the same activation observes the structural result before commit

#### Scenario: Observable iteration order is creation order
- **WHEN** a rule without `order by:` emits one event per entity and some entities were destroyed earlier
- **THEN** the events are queued in the creation order of the remaining entities on every backend

#### Scenario: Source layout does not change behavior
- **WHEN** a compiling program's rules are moved between files or its imports are reordered, with no other change
- **THEN** the program behaves identically

#### Scenario: Event timing does not depend on a backend setting
- **WHEN** the same cyclic program runs on two backends, or on one backend built with different internal limits
- **THEN** every occurrence is delivered in the same activation on both

### Requirement: ECS is the primary gameplay model, with explicit boundaries
Cactus SHALL remain ECS-first for gameplay and tool modeling: traits, rules, events, templates, hierarchy, spawning and filtering are the modeling tools for both.

Rendering submission, input devices, and other engine integration SHALL default to stdlib/backend layers. Application logic SHALL NOT. When a UI or tool can only be built by moving its application logic into `extern` code, such as spawning templates by string name, laying out widgets in native code, or inspecting traits in native code, that SHALL be treated as a language gap, not as an accepted boundary.

#### Scenario: Gameplay mechanic uses ECS constructs
- **WHEN** a platformer or shooter mechanic is authored in Cactus
- **THEN** it is expected to use traits, rules, events, and spawned entities as the primary modeling tools

#### Scenario: UI concern is not forced into the core language story
- **WHEN** a maintained example needs HUD, menu or editor behavior
- **THEN** it builds that behavior from traits, rules, hierarchy and stdlib modules, and adds no UI keywords to the language

#### Scenario: Application logic forced into extern code is a gap
- **WHEN** a UI or tool feature can only be written as an `extern func` or `extern rule` because the notation cannot express its logic
- **THEN** the project records it as a language gap to close, rather than documenting the extern as the intended design

### Requirement: Total operation semantics
All operations in Cactus SHALL be total: they are defined for all possible inputs and never have undefined behavior or stop the program.

Operations that take an `entity_id` argument are defined for stale handles: they produce safe no-ops or no-match results. Authors SHALL NOT be required to check entity validity before performing operations.

Numeric operations are defined for every value, as specified by `dsl-numeric-semantics`: integer arithmetic wraps, integer division by zero has a defined result, float operations follow IEEE-754, and float-to-integer conversion maps NaN and out-of-range values to defined integers. Authors SHALL NOT be required to guard a division or a conversion against special values to avoid undefined behavior.

#### Scenario: Total operations require no author-side null checks
- **WHEN** an author writes `add Frozen to f.target` and `f.target` may be stale
- **THEN** no compile error occurs and the backend generates any required validity guard automatically

#### Scenario: Integer division needs no zero guard
- **WHEN** an author writes `per_item = total / count` and `count` may be `0`
- **THEN** the program keeps running and `per_item` is `0` when `count` is `0`

#### Scenario: Overflow is not undefined
- **WHEN** an integer expression overflows the 32-bit range
- **THEN** the result is the wrapped value on every backend and platform

### Requirement: Common gameplay errors are prevented by construction
The language SHALL prefer forms in which common bugs cannot be written, or are compile errors, over forms that rely on author discipline. Facts the compiler can derive from declarations SHALL NOT be restated by authors, because each restatement can drift from the truth. Total semantics for `entity_id` is one instance of this rule; it applies to every construct.

Three error classes SHALL be ruled out by construction in authored code, and no feature SHALL reopen any of them:

1. **Memory management.** No authored construct allocates, frees, or aliases memory. Values are copied; storage belongs to entities, traits, constants and the runtime.
2. **Lifetime management.** No authored operation can observe a destroyed entity's storage. Entity handles are total, structural changes commit at defined boundaries, and owned descendants are destroyed with their owner.
3. **Data races.** No two handlers can access the same data in an undefined order. Handler contracts and their write kinds are derived from handler bodies. The compiler orders conflicting handlers when a meaningful rule decides the order, lets commuting writes run in any order, and rejects every other conflict; authors write no locks and no access annotations.

#### Scenario: Collision code does not depend on shape kinds
- **WHEN** an author asks whether, or where, two entities collide
- **THEN** the authored code names the entities' roles, not their collider shapes
- **AND** adding, removing, or changing a shape kind on an entity needs no change to rules that query it

#### Scenario: Acceleration bounds are derived, not written
- **WHEN** a rule uses a stdlib geometric query between two bindings
- **THEN** the author writes no separate "loose bound" predicate for acceleration
- **AND** the compiler derives any broad-phase bound from the query itself

#### Scenario: Nothing found is a typed value, not a sentinel
- **WHEN** a query or aggregation can find nothing
- **THEN** the language provides a typed neutral result, such as `Empty` or a reducer identity, so authors do not invent magic values like `-1000.0`

#### Scenario: Meaningless declarations are compile errors
- **WHEN** a combination of declarations has no defined runtime meaning
- **THEN** the compiler reports a source-located error instead of accepting it and doing nothing at runtime

#### Scenario: A rule that can never match is a compile error
- **WHEN** a rule outside the stdlib names traits in `each:` that no entity can ever carry together, given every template, named entity, `add`, `remove` and spawn in the program
- **THEN** the compiler reports an error at the rule instead of compiling a rule that never runs

#### Scenario: A filter or trigger that can never act is a compile error
- **WHEN** a rule outside the stdlib has a `without:` trait no matching entity can carry, or a trigger nothing in the program produces
- **THEN** the compiler reports an error at that filter or trigger

#### Scenario: Library code is judged by its own consistency
- **WHEN** a stdlib rule is consistent on its own but the program never reaches it
- **THEN** the program compiles and the rule is dropped, because importing a library is not a meaningless declaration

#### Scenario: No authored construct manages memory
- **WHEN** an author stores a struct, list or string in a trait field, local or event
- **THEN** no authored statement allocates or frees it, and changing the copy never changes another value

#### Scenario: A destroyed entity cannot be observed
- **WHEN** one handler destroys an entity that another handler still holds as an `entity_id`
- **THEN** every later operation through that handle is a defined no-op or no-match, and no handler reads freed storage

#### Scenario: Conflicting handlers are ordered without annotations
- **WHEN** two handlers of one activation conflict and one only writes what the other reads
- **THEN** the compiler orders the writer first from their derived contracts, and the author writes no lock or access declaration

#### Scenario: Commuting writes need no order
- **WHEN** two handlers of one activation both add to the same field with `+=`
- **THEN** the program compiles with no order between them, and the result is the same in every execution order

#### Scenario: An ambiguous order is rejected
- **WHEN** two handlers of one activation both assign the same field and nothing meaningful orders them
- **THEN** the compiler reports an error naming both handlers instead of choosing an order

#### Scenario: A feature that reopens an error class is rejected
- **WHEN** a proposal would let authored code allocate memory, outlive an entity's storage, or access shared data in an undefined order
- **THEN** it is revised until the error class stays closed, or it is rejected

### Requirement: The declarative/restricted-imperative boundary
The Cactus authoring surface SHALL be divided into two tiers:

**Tier 1 — Declarative**: assets, inputs, trait declarations, unit/template declarations, rule declarations, event declarations, module structure, and other structural gameplay description.

**Tier 2 — Restricted imperative**: world-changing behavior inside handlers, including field mutation, conditionals, event emission, spawn/destroy, trait add/remove, projected trait facts, bounded iteration and pure function calls. Pure functions also support local value computation, conditionals and finite list/range iteration; they cannot access or mutate world state or perform effects.

The following SHALL be explicitly out of scope for the authoring tier:
- general loops (`while`, numeric/indexed `for`, and other open-ended loop forms),
- open-ended recursion in user `func` bodies,
- mutable global or module-level state,
- direct memory management,
- unsafe operations.

Bounded `for item in list_expr:` and direct `for i in range(...):` iteration are permitted in handlers and pure functions. They capture a finite iteration domain once and do not introduce open-ended control flow. Function iterations permit only local value computation. Finite termination does not imply that every chosen bound has low execution cost.

A rule's `repeat: N` clause is not a general loop. It runs a rule pass a constant number of times, written in the program, so the number of passes is known at compile time.

#### Scenario: General loop construct rejected
- **WHEN** a `while` statement or numeric/indexed `for` loop appears in author code
- **THEN** the compiler SHALL report that general loops are not supported in the authoring tier

#### Scenario: Bounded foreach accepted in handler
- **WHEN** a rule event handler contains `for hit in hits:` and `hits` has type `list[T]`
- **THEN** the construct is evaluated as bounded snapshot iteration rather than as a general loop

#### Scenario: Func recursion rejected
- **WHEN** a `func` body calls itself directly
- **THEN** the compiler SHALL report that recursive calls are not allowed in pure `func` bodies

#### Scenario: Pure function computes a finite sample
- **WHEN** a pure function evaluates a bounded set of noise candidates using local accumulators
- **THEN** the computation is accepted without granting world access, effects, recursion or open-ended loops

#### Scenario: Repetition with a constant count is accepted
- **WHEN** a rule declares `repeat: 3`
- **THEN** the construct is accepted as bounded repetition, not as a general loop

#### Scenario: Repetition with a run-time count is rejected
- **WHEN** a rule declares `repeat:` with a count that reads a field
- **THEN** the compiler reports that the count must be a constant

### Requirement: The author/backend split
The following concerns SHALL be treated as backend or stdlib responsibilities rather than authored gameplay code:

| Concern | Responsibility |
|---|---|
| Rendering submission | backend / render stdlib |
| Physics integration | backend / physics stdlib |
| Audio playback plumbing | backend / audio stdlib |
| Entity validity guards | backend |
| Serialization | world capture/restore generated from persist and archetype declarations; encoding and storage supplied by external adapters |
| Input device mapping | `std.input` and runtime |
| Scene lifecycle plumbing | backend runtime |

Network replication has no authored surface yet. A future networking design SHALL be specified on its own and SHALL NOT be implied by a field modifier.

Persistence SHALL let authors mark durable field state and request operations through stdlib events without writing entity reconstruction, reference remapping, or serialization loops. Format choice SHALL remain outside gameplay declarations.

#### Scenario: Backend concern is not treated as core authoring syntax
- **WHEN** an example requires rendering or UI behavior
- **THEN** the project treats that concern as stdlib/backend-facing rather than as mandatory gameplay-core syntax

#### Scenario: Custom storage does not change gameplay schema
- **WHEN** a game switches its external persistence encoding
- **THEN** its persist fields and archetype declarations retain the same capture/restore meaning

### Requirement: Device and execution-target placement is a backend decision, never authored

Device and execution-target placement — which backend, code path, or execution unit realizes a
declared construct — SHALL always be derived by the compiler/backend from the construct's declared
data, used operations, and the selected backend's capabilities. It SHALL NEVER be expressed as an
author-written marker, keyword, or annotation on any declaration (a `gpu`, `shader`, `target`, or
`kind`-style clause is explicitly disallowed for this purpose, whether on a `phase`, `rule`, or any
other declaration). This requirement binds future placement-related language work as well as the
render-pass phase mechanism (`dsl-render-passes`), which has only one lowering path today and
therefore makes no placement choice yet — the requirement exists so a later change that does
introduce a real choice does not introduce such a marker to express it.

#### Scenario: A render-pass phase carries no device marker
- **WHEN** a `phase` is recognized as a render-pass phase (`dsl-render-passes`)
- **THEN** its declaration contains no keyword or field naming a device, target, or execution kind
  — only the `Pass`/`Target` descriptor fields, which name a rendering *pipeline shape*, not a
  device

#### Scenario: A future placement-choice proposal is evaluated against this requirement
- **WHEN** a future change proposes letting the backend choose between two or more lowering
  targets for the same construct (e.g. CPU vs. an additional GPU compute path for an ordinary
  rule)
- **THEN** the proposal SHALL be rejected if it expresses that choice as an author-written marker,
  regardless of how the eligibility analysis itself is designed

### Requirement: Performance is a backend obligation
The generated backend SHALL produce the most performant code derivable from the author's declarations. Authors SHALL NOT be required to hand-tune or annotate gameplay-core declarations for performance.

Because every rule domain, predicate, and write is declared, the backend sees the whole program: every query, every archetype, and which data is ever written. The choice of acceleration structures, specializations, and data layout SHALL belong to the backend, derived from that whole-program knowledge. The language SHALL NOT expose author-facing controls for these choices. When the backend cannot accelerate a declared query, the compiler SHALL say so with a diagnostic rather than degrade silently. A query is unaccelerated when its cost is superlinear in the entities it can match, or when it scans a whole domain for one key, as `query-cost-diagnostics` specifies. A pass that visits each row of its domain once is the cost of that pass, not a fallback.

#### Scenario: Filter generates typed view, not dynamic query
- **WHEN** a rule declares `each: Position as p, Velocity as v`
- **THEN** the EnTT backend SHALL generate a statically typed view rather than a runtime-reflective lookup

#### Scenario: Author declarations do not require performance annotations
- **WHEN** an author writes `each: Health as h` in a rule with many entities
- **THEN** no special performance annotation is required from the author

#### Scenario: Authors do not choose acceleration structures
- **WHEN** a pair rule asks which entities collide
- **THEN** no clause names a broad phase, tree, grid, or other structure
- **AND** the backend picks the strategy from the program's declarations

#### Scenario: Write analysis informs strategy without markers
- **WHEN** no handler in the program writes the transforms or shapes of some set of queried entities
- **THEN** the backend is free to treat that set as static, for example by building its acceleration structure once
- **AND** the author writes no `static`-style marker to allow this

#### Scenario: Unaccelerated queries are diagnosed
- **WHEN** a declared query has no acceleration the backend can apply
- **THEN** the compiler emits a warning naming the query, instead of silently falling back to a full scan

#### Scenario: A linear pass is not a fallback
- **WHEN** a rule has `each: Health as hp` and `where: hp.current < 30.0`
- **THEN** no acceleration diagnostic is reported, because the pass visits each row once

### Requirement: Feature evaluation criteria
All proposed changes to the language SHALL be evaluated against the following criteria, in order:
1. Is this an author concern or a backend concern?
2. Is it a genre-neutral primitive, or does it force engine plumbing or genre-specific syntax into authored code?
3. Does it remove a class of common errors, or introduce one? In particular, does it keep memory management, lifetime management and data-race errors impossible?
4. Does every definition that uses it stay correct: determinate, total, terminating and non-vacuous? Which property does the compiler enforce for it, and how?
5. Is its timing and behavior predictable without hidden backend knowledge?
6. Are its operations total and are failure semantics defined?
7. Does it preserve the declarative/restricted-imperative boundary, and does it prefer derived state over maintained state?
8. Does it preserve a small, teachable gameplay-core profile?

#### Scenario: Proposed feature is a backend concern
- **WHEN** a feature would require authors to write rendering, serialization, or lifetime-management code
- **THEN** the proposal SHOULD be redirected to a stdlib/backend change rather than expanding the core language surface

#### Scenario: Proposed feature expands imperative power
- **WHEN** a proposal adds new imperative constructs such as loops or mutable globals
- **THEN** it MUST provide strong justification for why the existing gameplay model, bounded foreach, events, and projected facts are insufficient

#### Scenario: Proposed feature adds an error-prone obligation
- **WHEN** a proposal requires authors to restate something the compiler could derive, such as a shape kind or an acceleration bound
- **THEN** it is revised so that the compiler derives it, or the proposal justifies why that is impossible

#### Scenario: Proposed feature is genre-specific
- **WHEN** a proposal adds a keyword or declaration that only makes sense for one genre or for UI, such as `widget`, `button` or `unit`
- **THEN** it is redirected to a genre-neutral primitive or a stdlib module

#### Scenario: Proposed feature names its correctness property
- **WHEN** a proposal adds a construct whose result could depend on an order, a value range, a cycle, or whether something matches
- **THEN** the proposal states which of the four properties is at risk and how the compiler rejects the incorrect uses

Projected traits and bounded foreach SHALL be evaluated as restricted gameplay constructs: `project` states current-frame facts for ECS filtering, while bounded foreach consumes finite query/list snapshots. Neither construct SHALL be treated as permission to add open-ended imperative scripting features by default.

### Requirement: Cactus primitives are genre-neutral
Language constructs SHALL describe data, relations, time and reactions, not genres or widgets. The same constructs SHALL serve action games, strategy, simulation, turn-based games and UI applications at small and large scale. Genre- and domain-specific vocabulary, such as characters, units, widgets or tiles, SHALL live in stdlib modules written in Cactus wherever the notation can express them.

#### Scenario: Stdlib vocabulary is written in Cactus
- **WHEN** a stdlib module provides genre or UI vocabulary, such as a character controller or a stack layout
- **THEN** its policy is written as Cactus rules over traits, and only program-independent kernels are native runtime code

#### Scenario: A genre need becomes a general primitive
- **WHEN** a genre-specific need, such as RTS target acquisition, motivates a language change
- **THEN** the change is stated as a general primitive, such as a relation or a derived trait, that other genres can use

### Requirement: Derived state is preferred over maintained state
When the language supports state that is a function of other state, it SHALL do so as derived state that the compiler keeps current, as `keep` does for pair relations. It SHALL NOT do so by documenting a pattern in which handlers maintain copies with flags, sentinels, or paired spawn and destroy code, because each such copy can drift from its source.

#### Scenario: Derivable fact is declared, not maintained
- **WHEN** a trait's presence or values follow from a declared relation, such as "touching lava"
- **THEN** the language offers a derived form whose single writer is the compiler, and handlers that set or clear that trait by hand are not the recommended form

#### Scenario: Derivation gives the backend more knowledge
- **WHEN** state is declared as derived
- **THEN** the backend knows its inputs and may choose when and how to recompute it, for example only when an input changed

### Requirement: A correct definition has four properties
A Cactus definition SHALL be called correct when it has all four of these properties:

1. **Determinacy**: it has exactly one meaning. For a given initial world and a given sequence of host inputs, every conforming backend on every platform produces the same sequence of worlds, events and effect calls. No result depends on source position, file or module order, storage order, or any backend choice the definition does not make.
2. **Totality**: that meaning is defined for every input. No operation has undefined behavior or stops the program, whatever the values it receives.
3. **Termination**: every activation finishes, for every world the definition can reach.
4. **Non-vacuity**: every declaration can affect the result. A declaration that can never run, never match, or never be observed is not part of a correct definition.

The goal SHALL be that a definition the compiler cannot show to be correct does not compile. Each rejection rule SHALL be specified by the capability that enforces it, and SHALL name the property it enforces. The four properties SHALL be stated without new syntax: authors write no annotations to establish them.

#### Scenario: A source-order dependence breaks determinacy
- **WHEN** moving a rule to another file or reordering `use` lines changes what a program does
- **THEN** the program is not correct by this definition

#### Scenario: Storage order dependence breaks determinacy
- **WHEN** the result of a frame depends on the order in which a backend's entity storage happens to hold entities
- **THEN** the program is not correct by this definition

#### Scenario: Undefined behavior breaks totality
- **WHEN** some input value makes an operation's result undefined
- **THEN** the program is not correct by this definition

#### Scenario: A rule that can never run breaks non-vacuity
- **WHEN** a rule's domain can never match any entity the program can create
- **THEN** the program is not correct by this definition

#### Scenario: Correctness needs no author annotations
- **WHEN** an author writes a correct definition
- **THEN** it contains no declaration whose only purpose is to prove one of the four properties

### Requirement: Known correctness gaps are listed
Every class of definition that the compiler accepts today although it may break one of the four properties SHALL be listed in this requirement, with the property it breaks. A change that makes the compiler reject a class, or makes the class correct by definition, SHALL remove its entry. A new feature SHALL NOT add an entry. A class found later that was already accepted SHALL be added as soon as it is found, and that addition is not a new feature.

No class is known today. The order of every result that crosses the native boundary is declared and enforced as `native-result-ordering` requires, so a native function or emitter cannot leak storage order without a failing test.

#### Scenario: Closing a gap removes its entry
- **WHEN** a change makes the compiler reject conflicts that only declaration order orients
- **THEN** that change removes the row for handlers ordered only by stable declaration order

#### Scenario: A feature may not add a gap
- **WHEN** a proposed feature would let the compiler accept a definition whose result depends on source position
- **THEN** the proposal is revised so the compiler rejects it or gives it one defined meaning

#### Scenario: A discovered gap is listed
- **WHEN** a review finds an accepted class of definitions whose result depends on storage order
- **THEN** a row naming that class and the property it breaks is added until a change closes it

#### Scenario: Merged lifecycle routes are listed
- **WHEN** a program declares two unordered `tick` rules whose events lead to `add Mark` on one entity, one through a handler on a cycle with the `on added Mark` handler and one through a handler off it
- **THEN** this requirement lists no gap for that class, because `dsl-trait-lifecycle-triggers` gives the delivery one route whatever the command order

#### Scenario: Unordered native world reads are listed
- **WHEN** one `tick` rule writes `WorldTransform.position` and another, with no order between them, reads it through `transform.world_position`
- **THEN** this requirement lists no gap for that class, because `handler-contracts` adds the native's field reads to the caller's contract, so the writer runs first

#### Scenario: Observers of a projection are listed
- **WHEN** one `tick` rule runs `project A` and another, with no order between them, has `without: A` or calls `query.exists[A]()`
- **THEN** this requirement lists no gap for that class, because `handler-contracts` orders the projector before every membership observer

### Requirement: Delayed occurrences belong to the producing clock
Every activation SHALL have a clock. A phase activation's clock is its phase: a non-periodic phase has one activation per batch, and a periodic phase has one activation per repetition. Every other activation — the load and unload boundaries, the startup arrival activation, and an activation of an event that no phase claims as its root — SHALL belong to the host clock.

An occurrence travelling along a delayed edge SHALL keep its recipient and SHALL be delivered only to the consumers its delayed edges name. When it was produced in a phase activation, it SHALL be delivered in that phase's next activation, at the start of that activation's event cascade: after the phase's own handlers and before any occurrence that activation emits. When it was produced in a host-clock activation, it SHALL be delivered as an activation of its own at the next host external-event drain, after the host events already queued for that drain. Delayed occurrences of one clock SHALL be delivered in the order they were produced. Occurrences still delayed when the program reaches teardown, including those the unload activation produces, SHALL be discarded without running any handler.

#### Scenario: A fixed-step chain advances one hop per repetition
- **WHEN** a `fixed_tick` handler emits `Knock` to an entity, the `Knock` handler re-emits `Knock` to itself, and one frame runs three `fixed_tick` repetitions
- **THEN** the `Knock` handler runs once in the first repetition, once in the second, and once in the third

#### Scenario: A periodic phase with no repetition holds its delayed occurrences
- **WHEN** a `fixed_tick` repetition produces a delayed occurrence and the next frame runs no `fixed_tick` repetition
- **THEN** the occurrence is delivered in the next `fixed_tick` repetition, whichever frame runs it

#### Scenario: Delayed occurrences run before the new activation's own emissions
- **WHEN** a `tick` activation starts with one delayed `Knock` and a `tick` handler emits `Ping`
- **THEN** the `Knock` consumer runs before the `Ping` consumer

#### Scenario: A delay out of the load boundary reaches the host clock
- **WHEN** an `on load` handler spawns a `Minion`, and an `on added Offspring` handler on a cycle spawns another `Minion`
- **THEN** the second `on added Offspring` delivery runs as its own activation at the first frame's external-event drain

#### Scenario: Teardown discards delayed occurrences
- **WHEN** the frame loop exits while a delayed occurrence is pending
- **THEN** no handler runs for it, before or after the unload activation

### Requirement: Allowed transitions are domain facts
A state's declared transitions SHALL be treated as a domain fact, like a field's range: they state what the model allows, which the compiler cannot derive because the handlers are what they constrain. They SHALL be optional per state. They are not proof annotations, because no correctness property of the four depends on them; they support model correctness. The compiler SHALL derive everything else about a transition, such as its possible sources.

#### Scenario: A correct program needs no arrows
- **WHEN** a state declares no transitions
- **THEN** a program that uses it can still be correct by the four properties

#### Scenario: Sources are derived, not declared
- **WHEN** an author writes `add S.V` in a rule with `each: S.W`
- **THEN** the author writes nothing else to say which variant the entity leaves

### Requirement: A declared singleton is a domain fact
`single trait` SHALL state what the model allows, like a field's range. It SHALL NOT be a performance control: it names no structure or strategy, and it changes what programs are legal by making a second carrier a fault. The compiler MAY use it as a size bound, because the runtime enforces it. An author SHALL NOT be required to write it, and a program without it SHALL stay correct and only keep its cost warnings.

#### Scenario: A singleton is chosen for its meaning
- **WHEN** a trait marks the one terrain of a game
- **THEN** `single` is valid because a second terrain would be a bug, and the bound is a consequence

#### Scenario: A singleton is not a tuning knob
- **WHEN** a proposal adds a keyword that only tells the backend how to store or search data
- **THEN** it is rejected under "Performance is a backend obligation", and `single` is no precedent for it

### Requirement: Meaning, memory and format are separate layers
Every value SHALL be described in three layers. Its *meaning* is the set of values it can hold and how operations on it round; the program text states it and the spec fixes it. Its *memory* is how a backend stores it; a backend MAY choose any storage, from any whole-program fact, that gives the same meaning bit for bit. Its *format* is how a save or any other message outside the running program encodes it; the format SHALL be a function of the meaning alone. The compiler SHALL NOT derive a value's meaning from how the program uses it. An encoding that holds fewer values than the meaning SHALL be used only for a value its receiver only presents.

#### Scenario: A storage choice does not change a save
- **WHEN** a backend stores an `int` field whose range fits in 16 bits in 16 bits
- **THEN** the field's meaning, its save encoding and the schema fingerprint are the same as with 32-bit storage

#### Scenario: Use does not change meaning
- **WHEN** a `float` field is only ever increased by a phase's constant `dt`
- **THEN** its meaning stays binary32 `float`; only an explicit type such as a duration gives it another meaning

#### Scenario: A lossy encoding for a gameplay value is rejected
- **WHEN** a proposal sends a gameplay position in fewer bits than its meaning to a receiver that simulates with it
- **THEN** the proposal is revised, because the receiver would compute with a different meaning

### Requirement: A kept fact costs what changes
A keep rule's evaluation work SHALL follow changes to its inputs, not the size of the world. A keep rule whose inputs did not change since its last run SHALL evaluate no rows. A keep rule whose rows read only the fields of their own bindings SHALL evaluate, per run, a number of rows that grows with the entities whose inputs changed and the rows they take part in, not with the size of its domain. Other keep rules SHALL evaluate fully only in runs where an input changed. Authors SHALL write nothing to obtain this. The compiler SHALL record, for every keep rule, which of the two cases it is in and, for a rule that evaluates fully, the construct that makes it so, and the cost report SHALL show both.

#### Scenario: A static keep does no work
- **WHEN** a keep rule derives `Neighbor` rows from `let` tile coordinates and no tile is spawned or destroyed
- **THEN** after its first run it evaluates no rows in later activations

#### Scenario: One moved entity
- **WHEN** one of 10,000 entities moves and a unary keep reads only that entity's position
- **THEN** the next run evaluates the rows of that entity only

#### Scenario: The reason for full evaluation is shown
- **WHEN** a keep's `where:` tests the derived relation `Reach(Root, u)` and the program is compiled with `--report-cost`
- **THEN** the report shows that keep as `full` and names the derived relation `Reach`

#### Scenario: A keep that reads the world
- **WHEN** a keep's `where:` tests a derived relation and nothing it reads changed
- **THEN** the run evaluates no rows
