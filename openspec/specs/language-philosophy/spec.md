## Purpose

Define the design philosophy and guiding principles behind the Cactus language, including its gameplay-core scope, predictability and total-semantics goals, ECS-centric model, declarative/imperative boundary, and the author/backend responsibility split.

## Requirements

### Requirement: Cactus language identity
Cactus SHALL be defined as a declarative, data-oriented gameplay description language that compiles to an engine backend. This identity statement SHALL be the authoritative reference for evaluating proposed language features.

The primary authoring audience is a game designer or gameplay programmer who wants to describe what exists, what reacts to what, how state changes, and how gameplay unfolds without writing engine plumbing, lifetime management, or null-guard boilerplate.

#### Scenario: Language identity governs feature evaluation
- **WHEN** a new language feature is proposed
- **THEN** the proposal MUST address whether the feature strengthens gameplay authoring or whether it belongs in the generated backend or stdlib instead

### Requirement: Simplicity is defined against a gameplay-core profile
The project's simplicity claims SHALL apply to a curated gameplay-core profile rather than to the union of every current, experimental, or deferred language idea.

The gameplay-core profile SHALL be small enough to teach as a coherent authoring model for action-game mechanics such as platformers and shooters.

#### Scenario: Simplicity claim refers to the gameplay core
- **WHEN** project documentation describes Cactus as simple or beginner-friendly
- **THEN** that claim refers to the curated gameplay-core surface rather than the full set of deferred or backend-facing ideas

#### Scenario: Core profile remains focused on teachable mechanics
- **WHEN** a new feature is evaluated for the main language story
- **THEN** it is assessed against whether it preserves a small, teachable gameplay-core model

### Requirement: Cactus is gameplay-focused, not a general-purpose engine scripting surface
The language SHALL be described as a gameplay-focused DSL optimized for expressing entity state, events, spawning, rule-driven updates, and action-game rules. It SHALL NOT be presented as a complete general-purpose solution for UI description, tool scripting, or arbitrary engine orchestration unless such features are explicitly designed and accepted.

#### Scenario: Gameplay-oriented feature fits the language identity
- **WHEN** a proposal strengthens player movement, projectiles, collision reactions, combat flow, or scene/gameplay state transitions
- **THEN** it is evaluated as part of the language's core mission

#### Scenario: Non-gameplay abstraction is treated cautiously
- **WHEN** a proposal introduces a broad abstraction for UI, engine plumbing, or unrelated scripting concerns
- **THEN** the project evaluates whether it belongs in stdlib/backend layers or a deferred capability instead of the gameplay core

### Requirement: Predictability is a first-class design goal
The Cactus execution model SHALL be fully predictable. Authors SHALL be able to reason about when and how every statement executes without consulting backend implementation details.

The following timing guarantees SHALL be documented and honored by all backends:
1. **Phase order**: declared `from:` and `after:` dependencies define activation barriers; the standard graph is input -> fixed_tick* -> tick -> late_tick -> render.
2. **Handler order within an activation**: explicit handler ordering and inferred contract-conflict edges define dependencies, with stable declaration order resolving otherwise ambiguous conflicts.
3. **Event delivery**: emitted events activate contracted consumer nodes within the current activation's bounded cascade; overflow follows the documented deferral rule.
4. **Structural change timing**: `add`, `remove`, `spawn`, and `destroy` commit after the complete handler/event cascade of each activation or fixed-step repetition.
5. **Fixed-step timing**: periodic cadence, catch-up cap, dropped excess time, fixed `dt`, and interpolation `alpha` follow phase declarations identically on every backend.
6. **External effects**: matching effect domains are observably serialized by graph order.
7. **`order by:` timing**: sorting occurs at a defined point before selected handler iteration and contributes trait reads to that handler contract.

#### Scenario: Phase barrier is invariant
- **WHEN** fixed_tick repeats during a frame
- **THEN** tick begins only after all fixed repetitions and their commits

#### Scenario: Contract order is invariant
- **WHEN** one handler writes a trait consumed by another in the same activation
- **THEN** every backend honors the corresponding handler graph dependency

#### Scenario: Structural visibility is invariant
- **WHEN** a handler queues a structural command
- **THEN** no handler in the same activation observes the structural result before commit

### Requirement: ECS is the primary gameplay model, with explicit boundaries
Cactus SHALL remain ECS-first for gameplay modeling, but the language philosophy SHALL explicitly distinguish gameplay concerns from presentation and engine-plumbing concerns.

Gameplay-facing constructs such as traits, rules, events, templates, spawning, and filtering belong in the core language identity. Presentation, UI, and engine integration concerns SHALL default to stdlib/backend layers unless a dedicated language feature is intentionally added.

#### Scenario: Gameplay mechanic uses ECS constructs
- **WHEN** a platformer or shooter mechanic is authored in Cactus
- **THEN** it is expected to use traits, rules, events, and spawned entities as the primary modeling tools

#### Scenario: UI concern is not forced into the core language story
- **WHEN** a maintained example needs HUD or menu behavior
- **THEN** the documentation does not treat missing first-class UI syntax as a failure of the gameplay-core language identity

### Requirement: Total operation semantics
All operations in Cactus that take an `entity_id` argument SHALL be total: they are defined for all possible inputs, including stale handles. Operations on stale handles produce safe no-ops or no-match results. Authors SHALL NOT be required to check entity validity before performing operations.

#### Scenario: Total operations require no author-side null checks
- **WHEN** an author writes `add Frozen to f.target` and `f.target` may be stale
- **THEN** no compile error occurs and the backend generates any required validity guard automatically

### Requirement: The declarative/restricted-imperative boundary
The Cactus authoring surface SHALL be divided into two tiers:

**Tier 1 — Declarative**: assets, inputs, trait declarations, unit/template declarations, rule declarations, event declarations, module structure, and other structural gameplay description.

**Tier 2 — Restricted imperative**: behavior inside handlers only, including field mutation, conditionals, event emission, spawn/destroy, trait add/remove, projected trait facts, bounded list iteration, and pure function calls.

The following SHALL be explicitly out of scope for the authoring tier:
- general loops (`while`, numeric/indexed `for`, and other open-ended loop forms),
- open-ended recursion in user `func` bodies,
- mutable global or module-level state,
- direct memory management,
- unsafe operations.

Bounded `for item in list_expr:` iteration is permitted as a restricted handler construct because it iterates a finite list snapshot and does not introduce open-ended control flow.

#### Scenario: General loop construct rejected
- **WHEN** a `while` statement or numeric/indexed `for` loop appears in author code
- **THEN** the compiler SHALL report that general loops are not supported in the authoring tier

#### Scenario: Bounded foreach accepted in handler
- **WHEN** a rule event handler contains `for hit in hits:` and `hits` has type `list[T]`
- **THEN** the construct is evaluated as bounded snapshot iteration rather than as a general loop

#### Scenario: Func recursion rejected
- **WHEN** a `func` body calls itself directly
- **THEN** the compiler SHALL report that recursive calls are not allowed in pure `func` bodies

### Requirement: The author/backend split
The following concerns SHALL be treated as backend or stdlib responsibilities rather than authored gameplay code:

| Concern | Responsibility |
|---|---|
| Rendering submission | backend / render stdlib |
| Physics integration | backend / physics stdlib |
| Audio playback plumbing | backend / audio stdlib |
| Entity validity guards | backend |
| Serialization | generated from `persist` |
| Network replication | generated from `sync` |
| Input device mapping | `std.input` and runtime |
| Scene lifecycle plumbing | backend runtime |

#### Scenario: Backend concern is not treated as core authoring syntax
- **WHEN** an example requires rendering or UI behavior
- **THEN** the project treats that concern as stdlib/backend-facing rather than as mandatory gameplay-core syntax

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

#### Scenario: Filter generates typed view, not dynamic query
- **WHEN** a rule declares `filter: Position as p, Velocity as v`
- **THEN** the EnTT backend SHALL generate a statically typed view rather than a runtime-reflective lookup

#### Scenario: Author declarations do not require performance annotations
- **WHEN** an author writes `filter: Health as h` in a rule with many entities
- **THEN** no special performance annotation is required from the author

### Requirement: Feature evaluation criteria
All proposed changes to the language SHALL be evaluated against the following criteria, in order:
1. Is this an author concern or a backend concern?
2. Does it strengthen gameplay authoring or force engine plumbing into authored code?
3. Is its timing and behavior predictable without hidden backend knowledge?
4. Are its operations total and are failure semantics defined?
5. Does it preserve the declarative/restricted-imperative boundary?
6. Does it preserve a small, teachable gameplay-core profile?

#### Scenario: Proposed feature is a backend concern
- **WHEN** a feature would require authors to write rendering, serialization, or lifetime-management code
- **THEN** the proposal SHOULD be redirected to a stdlib/backend change rather than expanding the core language surface

#### Scenario: Proposed feature expands imperative power
- **WHEN** a proposal adds new imperative constructs such as loops or mutable globals
- **THEN** it MUST provide strong justification for why the existing gameplay model, bounded foreach, events, and projected facts are insufficient

Projected traits and bounded foreach SHALL be evaluated as restricted gameplay constructs: `project` states current-frame facts for ECS filtering, while bounded foreach consumes finite query/list snapshots. Neither construct SHALL be treated as permission to add open-ended imperative scripting features by default.

