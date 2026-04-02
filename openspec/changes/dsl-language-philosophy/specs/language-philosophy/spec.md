## ADDED Requirements

### Requirement: Cactus language identity
Cactus SHALL be defined as a **declarative, data-oriented gameplay description language that compiles to an engine backend**. This identity statement SHALL be the authoritative reference for evaluating all proposed language features.

The primary authoring audience is a game designer or gameplay programmer who wants to describe *what exists*, *what reacts to what*, *how state changes*, and *what is presented*. The language SHALL NOT require authors to write engine plumbing, lifetime management, null checks, rendering calls, or physics integration.

#### Scenario: Language identity governs feature evaluation
- **WHEN** a new language feature is proposed
- **THEN** the proposal MUST address whether the feature helps authors describe mechanics/presentation or whether it belongs in the generated backend

### Requirement: Predictability is a first-class design goal
The Cactus execution model SHALL be fully predictable. Authors SHALL be able to reason about when and how every statement executes without consulting the backend implementation. The following timing guarantees SHALL be documented and honored by all backends:

1. **Phase order**: input → fixed_tick → tick → late_tick → render. This order is fixed and cannot be reordered.
2. **System order within a phase**: Deterministic. Defined by `after:` constraints and declaration order. Cycles are a compile-time error.
3. **Event delivery**: Events are delivered within the current phase's cascade depth. Events that would exceed the cascade depth from `late_tick` handlers are deferred to the next frame.
4. **Structural change timing**: `add Trait`, `remove Trait`, `spawn`, `destroy` take effect before the next system handler invocation in the same phase. They are not batched to end-of-frame.
5. **`order by:` sort timing**: Sorting occurs once before each handler's iteration loop, within the current frame.

#### Scenario: Phase order is invariant
- **WHEN** a program declares systems with `on input:`, `on tick:`, and `on late_tick:` handlers
- **THEN** all `on input:` handlers execute before all `on tick:` handlers, which execute before all `on late_tick:` handlers

#### Scenario: System order within phase is deterministic
- **WHEN** two systems have `after:` ordering constraints
- **THEN** the constrained system always executes after the system it depends on, within the same phase

### Requirement: Total operation semantics
All operations in Cactus that take an `entity_id` argument SHALL be **total**: they are defined for all possible inputs, including stale handles. Operations on stale handles produce safe no-ops or no-match results. Authors SHALL NOT be required to check entity validity before performing operations. The generated backend inserts all necessary validity guards.

The canonical principle is:
> The author describes valid game relationships and rules. The generated backend is responsible for making all operations safe, deterministic, and non-nullable at the authoring level.

#### Scenario: Total operations require no author-side null checks
- **WHEN** an author writes `add Frozen to f.target` and `f.target` may be stale
- **THEN** no compile error occurs; the backend generates a validity guard; no author-side check is required

### Requirement: The declarative/restricted-imperative boundary
The Cactus authoring surface SHALL be divided into two tiers:

**Tier 1 — Declarative** (description, no runtime behavior):
Assets, inputs, trait declarations, unit/template declarations, system declarations (filter, exclude, order by, after), event declarations, config blocks, module structure.

**Tier 2 — Restricted imperative** (behavior inside event handlers only):
Field mutation, conditional logic (`if`/`else`), value pattern matching, event emission, spawn/destroy, dynamic trait add/remove, simple arithmetic, pure function calls.

The following SHALL be explicitly out of scope for the authoring tier:
- General loops (`for`, `while`) — iteration is the system/filter mechanism's job
- Open-ended recursion in `func` bodies
- Mutable global or module-level state
- Direct rendering API calls (eventual goal: rendered via traits + generated systems)
- Direct memory allocation
- Unsafe operations

#### Scenario: Loop construct rejected
- **WHEN** a `for` or `while` statement appears in author code
- **THEN** the compiler SHALL report: "loops are not supported; use the system/filter mechanism for iteration"

#### Scenario: Func recursion rejected
- **WHEN** a `func` body calls itself directly
- **THEN** the compiler SHALL report: "recursive calls are not allowed in pure func bodies"

### Requirement: The author/backend split
The following concerns SHALL be the responsibility of the **generated backend and stdlib**, not of authored code:

| Backend concern | Source of authority |
|---|---|
| Rendering submission | Generated from `Sprite`, `Mesh`, etc. trait presence |
| Transform propagation | Generated from `Transform` and parent-child relationships |
| Audio playback | Generated from `AudioTrack` trait |
| Physics integration | Generated from `Body`, `Velocity` traits |
| Entity validity guards | Generated for all cross-entity operations |
| Serialization | Generated from `persist` field modifier |
| Network replication | Generated from `sync` field modifier |
| Input device mapping | Abstracted by `std.input` stdlib |
| Scene lifecycle plumbing | Managed by the backend runtime |

When authored code contains explicit rendering calls (`draw_rect`, `draw_sprite`), explicit physics integration, or explicit validation checks, this indicates a gap in the stdlib/backend, not a correct authoring pattern. Such gaps SHALL be tracked as future changes.

#### Scenario: Backend concern gap identified
- **WHEN** a `dsl_showcase.cactus` or example contains explicit `draw_rect` calls in system handlers
- **THEN** this is documented as a known stdlib gap (declarative presentation not yet complete), NOT as the intended authoring pattern

### Requirement: Performance is a backend obligation
The generated backend SHALL produce the most performant code derivable from the author's declarations. Authors SHALL NOT be required to hand-tune, restructure, or annotate their declarations for performance. The backend is responsible for exploiting all static information present in the declaration to generate optimal code.

Specific backend performance obligations:

| Declaration | Generated code obligation |
|---|---|
| `filter: A, B` | Typed compile-time view — no runtime component lookup |
| `exclude: X` | Zero-overhead exclusion mask in the view |
| `on event_name:` | Direct typed dispatch — no dynamic handler lookup |
| `add Trait` / `remove Trait` | Direct typed `emplace_or_replace`/`remove` — no name-based lookup |
| `order by: a.f asc, b.g desc` | Single comparator, single sort pass — not N separate sorts |
| Marker trait | Zero storage — tag component only |
| `persist` field modifier | Optimized serialization path — no reflection overhead |
| `after:` ordering | Statically resolved at compile time — no runtime scheduler cost |
| Validity guard (`add Trait to id`) | O(1) generation-based check — no traversal |

The declarative model provides MORE static information than a general-purpose runtime ever would. Backends MUST exploit this. Because filters, events, and ordering are statically known at compile time, generated code can be as efficient as — or more efficient than — hand-written C++ ECS code.

**Performance constraints are backend design constraints, not authoring constraints.** If generated code for a particular pattern is too slow, the backend's code generation is improved. The author's declarations do not change to accommodate performance concerns.

#### Scenario: Filter generates typed view, not dynamic query
- **WHEN** a system declares `filter: Position as p, Velocity as v`
- **THEN** the EnTT backend SHALL generate `auto view = registry.view<Position, Velocity>()` — a statically-typed view with no runtime type resolution

#### Scenario: Author declarations do not require performance annotations
- **WHEN** an author writes `filter: Health as h` in a system with thousands of entities
- **THEN** no special annotation is required; the backend generates the most efficient iteration for that component set

#### Scenario: Declarative description produces zero-overhead abstractions
- **WHEN** a marker trait is declared and used in `filter:` or `exclude:`
- **THEN** the generated code uses a tag component with zero storage allocation; entity iteration cost is equivalent to hand-written C++

### Requirement: Feature evaluation criteria
All proposed changes to the Cactus language SHALL be evaluated against the following criteria, in order:

1. Is this an author concern or a backend concern? Backend concerns belong in the stdlib/backend, not the language surface.
2. Does it help authors describe mechanics or presentation, or does it require them to write engine plumbing?
3. Is its timing and behavior predictable without consulting the backend implementation?
4. Are all its operations total? Are failure semantics defined?
5. Does it expand or restrict imperative power in the authoring tier? Expansions require strong justification.
6. Does it belong in the declarative tier or the restricted-imperative tier? Features that blur this boundary require explicit justification.

#### Scenario: Proposed feature is a backend concern
- **WHEN** a proposed feature would require authors to write rendering, serialization, or lifetime management code
- **THEN** the proposal SHOULD be rejected or redirected to a stdlib/backend change instead

#### Scenario: Proposed feature expands imperative power
- **WHEN** a proposed feature adds new imperative constructs (e.g., loops, mutable globals)
- **THEN** the proposal MUST provide a strong justification for why the system/filter/event mechanism cannot address the use case
