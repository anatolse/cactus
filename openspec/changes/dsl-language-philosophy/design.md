## Context

This document captures the philosophy of the Cactus DSL as derived from its actual use, the architectural review, and the design decisions made across all change proposals to date. It is descriptive (what the language is) and prescriptive (what it should remain).

This document does not introduce new syntax or change compiler behavior. It is the reference standard for evaluating future changes.

---

## Goals / Non-Goals

**Goals:**
- State the language identity precisely enough to answer "should this be in the language?" for any proposed feature
- Define the author/backend split explicitly
- Define what is declarative, what is restricted-imperative, and what is explicitly out of scope
- Establish the execution model guarantees that all backends must honor
- Provide a heuristic for evaluating new feature proposals

**Non-Goals:**
- Providing implementation guidance (that belongs in design docs for specific changes)
- Being exhaustive about every possible feature (it is a guiding document, not an exhaustive spec)
- Overriding existing specs retroactively — conflicts are identified as future changes

---

## The Language Identity

Cactus is a **declarative, data-oriented gameplay description language** that compiles to an engine backend.

It is NOT:
- A general-purpose game programming language
- A scripting engine
- A visual scripting system
- A runtime virtual machine language
- A C++ replacement

The intended author is a game designer or gameplay programmer who wants to describe:
- **What exists** (entities, traits, assets, scenes)
- **What reacts to what** (systems, event handlers, filters)
- **How state changes** (mechanics rules, progression, reactions)
- **What is presented** (render layer, sprite, audio, UI)

The author should NOT need to write:
- Memory management or lifetime logic
- Null/validity checks for entity references
- Physics integration loops
- Render submission plumbing
- Input device mapping
- Serialization for persist fields
- Network replication for sync fields

---

## Decisions

### Decision 1: Predictability over expressiveness

The language does fewer things, but each thing is obvious.

This means:
- **Clear system timing**: input → fixed_tick → tick → late_tick → render. Always.
- **Clear event timing**: events delivered within the current phase's cascade depth. Deferred to next frame when limit reached.
- **Clear trait add/remove timing**: structural changes take effect before the next handler invocation.
- **Clear spawn/destroy timing**: spawned entities become active before the next handler invocation; destroyed entities are removed before the next handler invocation.
- **Deterministic system order**: within a phase, systems execute in a deterministic order defined by `after:` constraints and declaration order.

When in doubt, predictability wins over power.

### Decision 2: Total operations — the backend is responsible for safety

The language exposes no null entity references and no invalid-access operations. All operations on `entity_id` values are **total**: operations on stale handles are defined as safe no-ops or no-match. The generated backend inserts validity guards.

This principle generalizes:
> The author describes valid game relationships and rules. The generated backend is responsible for making all operations safe, deterministic, and non-nullable at the authoring level.

Authors never write defensive lifetime plumbing. They describe mechanics. The backend makes them safe.

### Decision 3: The declarative / restricted-imperative split

Two tiers of authoring:

**Tier 1 — Declarative (pure description):**
- Asset declarations
- Input declarations
- Trait declarations (field shapes, defaults)
- Unit and template declarations (entity blueprints)
- System declarations (filter, exclude, order by, after)
- Scene structure (module imports, units)
- Event declarations (event shapes)
- Config blocks (initial values)

These describe *what exists* and *what relationships hold*. They contain no runtime behavior.

**Tier 2 — Restricted imperative (behavior):**
- Event handler bodies
- State transitions (`var x = y`, field mutation)
- Conditional logic (`if`/`else`)
- Pattern matching (`match`)
- Event emission (`emit`)
- Spawning/destroying entities (`spawn`, `destroy`)
- Dynamic trait attachment/detachment (`add`, `remove`)
- Simple math (arithmetic expressions, function calls to pure `func`)

These describe *how state changes in response to events*. They are restricted: no loops, no open-ended recursion, no arbitrary global mutation, no direct world access outside handlers.

### Decision 4: What belongs in the generated backend, not author code

These concerns are **generated backend responsibilities**, not authoring concerns:

| Concern | How it's handled |
|---|---|
| Rendering submission | Backend auto-generates from Sprite/Mesh traits |
| Transform propagation | Backend generates parent-child matrix cascades |
| Audio playback | Backend generates from AudioTrack trait |
| Physics integration | Backend generates from Body/Velocity traits |
| Entity validity checks | Backend wraps cross-entity ops with `registry.valid()` |
| Serialization | Backend generates from `persist` field modifier |
| Network replication | Backend generates from `sync` field modifier |
| Input device mapping | `std.input` stdlib abstracts device bindings |
| Scene lifecycle plumbing | Backend manages spawn/destroy ordering |

When authors find themselves writing these concerns manually (e.g., explicit `draw_rect` calls), that is a signal the relevant stdlib module needs a better generated backend, not that authors should write more code.

### Decision 5: Explicit non-goals for the authoring layer

The following are explicitly OUT OF SCOPE for Cactus author code:

- **Loops** (`for`, `while`, `loop`) — iteration is provided by the system/filter mechanism
- **Unrestricted recursion** — `func` bodies cannot recurse
- **Mutable global state** — no global variables; state lives in traits on entities
- **Direct memory allocation** — no `new`, no `malloc`
- **Direct rendering API calls** — render via traits + generated systems (eventual goal)
- **Pointer arithmetic or unsafe operations** — none
- **Open-ended world queries** — no `for entity in world` loops outside the filter mechanism

### Decision 6: Performance is a backend obligation, not an author concern

The author trusts the backend to produce the most performant code possible from their declarations. Authors SHOULD NOT need to hand-tune or rewrite generated code for performance. This is the core value proposition: the author describes intent; the backend generates optimal executable code.

This is not aspirational — it is a concrete obligation with specific guarantees:

| Declaration | Backend performance obligation |
|---|---|
| `filter: A, B` on a system | Generate a typed compile-time view `registry.view<A, B>()` — zero-cost abstraction, no runtime type lookup |
| `exclude: X` on a system | Include `entt::exclude_t<X>` in the view — zero overhead filtering |
| `on event_name:` handler | Generate direct typed dispatch — no dynamic handler lookup, no string matching |
| `add Trait(...)` | Generate direct `emplace_or_replace<T>()` — no runtime trait lookup by name |
| `remove Trait` | Generate direct `registry.remove<T>()` |
| `order by: a.field asc, b.field desc` | Generate a single lexicographic comparator — one sort pass, not N separate passes |
| Marker traits (no fields) | Zero storage allocated — tag component only |
| `persist` field modifier | Backend generates optimized serialization path — no reflection overhead |
| `sync` field modifier | Backend generates delta-sync path — only changed fields are transmitted |
| `after:` system ordering | Statically resolved at compile time — no runtime scheduler overhead |
| Validity guards (`add to id`) | `registry.valid(id)` is O(1) — generation-based handles, no linked list traversal |

**The declaration model enables better performance than hand-written code in several cases:**
- Because filters are statically declared, the backend can generate perfectly typed views with no dynamic component lookup
- Because event subscriptions are statically declared, the backend can generate direct call dispatch with no hash tables or dynamic dispatch overhead
- Because ordering is statically declared, the backend can reorder system execution at compile time rather than resolving priorities at runtime
- Because trait membership is statically typed, the backend can use struct layout and cache-friendly SoA storage

Performance and safety do not conflict. `registry.valid()` guards are O(1). Sorting uses a single pass. The author never pays for abstractions they did not need.

**When a performance constraint exists, it is a backend design constraint, not an authoring constraint.** Authors never add performance comments ("// this is slow") to their game logic. If a pattern is too slow, the backend improves its code generation — the author's declarations do not change.

### Decision 7: Feature evaluation heuristic

When evaluating a proposed feature, ask:

1. **Author concern or backend concern?** If the author shouldn't need to think about it, it belongs in the generated backend.
2. **Does it help describe mechanics/presentation, or does it help write engine plumbing?** If plumbing, decline.
3. **Is it predictable?** Can the author reason about when and how it executes? If timing is unclear, define it clearly before accepting.
4. **Is it total?** Can it fail in undefined ways? If yes, define the safe failure semantics before accepting.
5. **Does it restrict or expand imperative power?** Prefer restrictions over expansions. Expansions need strong justification.
6. **Does it fit the declarative tier or the restricted-imperative tier?** Features that blur the boundary need careful evaluation.

---

## Risks / Trade-offs

- **The philosophy is aspirational in parts.** The current implementation has `draw_rect` calls and explicit physics integration in author code. These are acknowledged gaps, not violations — they exist because the stdlib rendering pipeline is not yet complete. The philosophy describes where the language is going, not solely where it is now.

- **Restricted imperative is powerful enough to drift toward scripting.** Without the explicit non-goals (no loops, no mutable globals), the imperative tier can expand unchecked. The non-goals list must be actively maintained.

- **The backend/author split requires investment in stdlib.** The more we move rendering, audio, and physics into generated backend systems, the less authors have to write — but the more the stdlib must do. This is intentional: the language's value proposition is that the stdlib and backend handle the plumbing.
