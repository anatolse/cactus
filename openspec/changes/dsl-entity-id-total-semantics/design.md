## Context

The current `entity_id` spec says handles "always refer to a live entity." This is aspirationally true for freshly-spawned handles captured in a `let` binding within a single frame, but it breaks down the moment a handle is stored in a trait field (`var target: entity_id`) and the referenced entity is later destroyed. The spec offers no model for what happens then — it just silently promises it won't happen.

This creates a gap between the spec and reality. Authors who store `entity_id` in traits (e.g., `Follower.target`, `Parent.parent_id`) will encounter stale handles in practice. The language must define what happens.

The solution is the **total operations model**: every operation that takes an `entity_id` is defined for all possible inputs, including stale/dead handles. The result is always a safe no-op or a no-match. Authors never observe undefined behavior, memory corruption, or runtime crashes from stale handles. They also never need to write null-checks. The backend takes responsibility for the validity guard.

### Why total operations and not option types or checked access

Alternative approaches considered:

| Approach | Problem |
|---|---|
| `entity_id?` nullable type | Forces null-checks everywhere; defeats author-friendliness |
| `try_get` / `maybe` syntax | Too verbose for a gameplay DSL; C++ complexity leaks upward |
| "handle always valid" (current) | Untrue; silently undefined for stale fields |
| **Total operations (chosen)** | Authors never check; backend handles validity; clean model |

The total operations model gives the best author experience while being fully implementable with EnTT generation-based entity IDs.

### EnTT implementation

EnTT uses generation-based entity IDs. `registry.valid(entity)` returns `false` if the entity was destroyed. This is O(1). All generated code for cross-entity operations simply wraps the operation:

```cpp
if (registry.valid(target)) {
    registry.emplace_or_replace<Stunned>(target, Stunned{...});
}
```

This is trivial to generate and is exactly what a careful C++ programmer would write manually.

### `exists(entity_id)` — the escape hatch

For the rare cases where authors need to branch on handle validity (e.g., "if my target is still alive, pursue; otherwise pick a new target"), `exists(entity_id)` provides a clean DSL expression:

```cactus
system FollowerSystem:
    filter:
        Position2D as pos
        Follower as f

    on tick:
        if exists(f.target):
            # safe to use f.target here — entity is live
            emit Follow(f.target)
        if not exists(f.target):
            # target died — find a new one or remove Follower
            remove Follower
```

`exists(entity_id)` compiles to `registry.valid(id)`. It returns `bool`. It is a built-in expression, not a user-callable function. The semantic analyzer resolves it from the type of its argument.

### What "absence" means without null

With `add`/`remove` (from `dsl-dynamic-traits`), the absence of a relationship is modeled by the absence of a trait, not a null value. The `exists()` function covers the remaining case: "I have a stored handle, is the entity still there?"

The two patterns together cover all relationship modeling:
1. **Trait presence**: use `Follower` with `exclude: Follower` to express "not following"
2. **Handle validity**: use `if exists(f.target):` when you have a stored entity reference

## Goals / Non-Goals

**Goals:**
- Remove the untrue "always live" guarantee
- Define total safe semantics for all `entity_id` operations on stale handles
- Add `exists(entity_id) bool` expression
- Update generated backend code to wrap cross-entity operations with validity guards
- Keep the author-facing model nil-free (no null literal, no explicit null checks required)

**Non-Goals:**
- Changing the in-memory representation of `entity_id` (still a value type, still EnTT `entt::entity`)
- Adding lifecycle callbacks for "entity I was watching just died" (observer pattern — future work)
- Making `exists()` callable in pure `func` bodies (requires world access — restricted to system handlers)

## Decisions

### Decision 1: Operations are safe no-ops, not panics

Using a stale handle is defined as a safe no-op, not a runtime error or assertion failure.

**Rationale**: Panicking on stale handles would force authors to audit every stored `entity_id` field defensively. That defeats the purpose of the total-operations model. The author experience should be: things just work, even if a referenced entity died last frame.

Debug builds MAY emit a warning when a stale handle is used. Release builds are always silent.

### Decision 2: `match stale_id:` yields no match

When `match entity_id:` is evaluated and the entity is dead, no arm fires (not even `_ =>`). The wildcard arm `_ =>` means "entity is live but no listed trait matched," not "entity is dead or no match."

**Rationale**: The wildcard semantics should be predictable. Authors who write `_ =>` expect to handle unknown trait combinations, not dead entities. Silently executing `_ =>` for dead entities would cause unexpected behavior (e.g., dealing damage to a dead entity).

If an author wants to handle the "dead entity" case, they use `if exists(c.other): match c.other: ...`

### Decision 3: `exists()` is a system-handler-only expression

`exists(entity_id)` requires world access (calling `registry.valid()`). It is not a pure expression and therefore cannot appear in `func` bodies. It is restricted to system event handler bodies.

### Decision 4: `entity_id == 0` error message updated

The old error message said "use trait enable/disable to model absent relationships." With `add`/`remove`, that advice is stale. The new message says "use `exists(id)` to test handle validity or `add`/`remove` to model absent relationships via trait presence."

## Risks / Trade-offs

- **Debug-only validity warnings** require the backend to track and report stale handle usage. This is not required but recommended for tooling. Not a compiler concern.

- **`match stale_id:` no-match** may surprise authors who expected `_ =>` to fire. Clear documentation required.

- **Silently dropped targeted events** may be hard to debug if an author stores a stale handle and wonders why their event wasn't delivered. Again, debug-mode logging in the backend can help.
