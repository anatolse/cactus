## Context

The current projected-traits code generation made projected values live outside the EnTT registry:

```text
project T to e
    │
    ▼
<prefixed projected overlay map>[e] = T{...}
    │
    ▼
generated systems scan every entity and manually check projected OR durable state
```

That design has two problems:

1. It bypasses EnTT's normal view filtering, requiring every generated system to scan all live entities.
2. The generated manual filter guard currently emits `return`, which exits the whole system handler when one entity does not match.

The backend already had a simpler and safer durable-only path:

```cpp
auto view = registry.view<A, B>(entt::exclude<C>);
view.each([&](entt::entity entity, A& A_comp, B& B_comp) {
    // handler body
});
```

The core design move is to make projected traits participate in the registry itself during the frame, so normal EnTT views remain correct.

## Goals / Non-Goals

**Goals:**
- Restore ordinary system iteration to EnTT `registry.view<...>()`-based filtering.
- Make projected traits visible to any code path that checks normal registry components during the current frame.
- Preserve frame-local cleanup semantics for projected facts.
- Preserve durable components that existed before projection.
- Keep repeated projections coalesced per `(entity, trait)`.

**Non-Goals:**
- Changing author-facing `project` syntax.
- Making projected traits persistent across frames.
- Adding multi-fact/accumulating projected trait records.
- Reworking scheduling or `after:` semantics.

## Decisions

### Decision: `project` writes to the EnTT registry

Generated code for `project Trait to target` should write or patch `Trait` directly in `entt::registry` using the same component type as durable traits.

Conceptually:

```text
project DamageFlash to e
    │
    ├─ if this is the first projection for (e, DamageFlash) this frame:
    │      remember whether e already had durable DamageFlash
    │      if yes, save its previous value
    │
    └─ emplace_or_replace / patch DamageFlash in registry
```

This makes projected traits visible to:
- `registry.view<DamageFlash>()`
- `registry.all_of<DamageFlash>(entity)`
- `registry.try_get<DamageFlash>(entity)`
- native EnTT `exclude` filters
- backend-owned stdlib extern systems that use normal registry views

### Decision: Cleanup restores or removes registry components

Because a projection may temporarily override a component that existed before the frame, cleanup needs to know which entities were touched and whether a previous durable value existed.

Per trait, generated helper state can be modeled as:

```cpp
std::vector<entt::entity> projected_T_entities;
std::unordered_map<entt::entity, std::optional<T>> projected_T_previous;
```

The exact container choice and spelling may vary, but generated helper identifiers should not use the `cactus` prefix. The semantics are:

```text
clear projected traits:
    for each tracked entity e for trait T:
        if previous durable T existed:
            registry.emplace_or_replace<T>(e, previous_value)
        else if registry.valid(e):
            registry.remove<T>(e)
    clear tracking state
```

Diagram:

```text
Before frame              During frame                 Cleanup
────────────              ────────────                 ───────
e1: Health          ──▶   e1: Health + Flash     ──▶   e1: Health
e2: Health+Flash0   ──▶   e2: Health + Flash1    ──▶   e2: Health+Flash0
```

### Decision: Generated systems should return to native EnTT views

Ordinary generated systems should not scan `registry.storage<entt::entity>()` for projected-trait support. Since projected traits are now registry components during the frame, the backend can use native views again:

```cpp
auto view = registry.view<Health, DamageFlash>(entt::exclude<Suppressed>);
view.each([&](entt::entity entity, Health& Health_comp, DamageFlash& flash) {
    // body
});
```

This fixes the early-exit bug and preserves efficient filtering.

### Decision: First projection snapshots once

Repeated projections to the same `(entity, trait)` in one frame should not overwrite the saved previous durable value. Only the first projection snapshots the pre-frame state.

```text
durable T0
project T = T1    saves T0
project T = T2    keeps saved T0, patches registry to T2
cleanup           restores T0
```

### Decision: Generated helper identifiers avoid the `cactus` prefix

New generated code introduced for registry-backed projected traits should use local, purpose-specific helper names without a `cactus` prefix. The helpers are generated into the project output and should read as implementation details of that generated translation unit, for example `projected_T_entities`, `projected_T_previous`, or `clear_projected_traits(...)` rather than prefixed global-looking names.

### Decision: Explicit `remove` after projection remains an implementation-sensitive edge case

If a later handler explicitly removes a trait that was projected earlier in the same frame, cleanup could either restore the pre-frame durable component or treat the explicit remove as winning. The safer v1 behavior should be specified and tested during implementation.

Recommended default: explicit `remove` should be durable intent and should cancel any pending restore for that `(entity, trait)`. This avoids resurrecting a component that authored code deliberately removed.

## Risks / Trade-offs

- **Registry mutation during view iteration:** projecting a trait while iterating a view may mutate EnTT storage. Implementation should avoid invalidating the active iteration path where possible and add tests for projecting traits both in and outside the active filter.
- **More cleanup bookkeeping:** registry-backed projection needs previous-value tracking. This is more complex than clearing a map, but it restores native filtering and protects durable state.
- **Durable/projected distinction is backend-internal:** during the frame, projected components are indistinguishable from durable components to normal registry queries. This is intentional, with cleanup providing the transient boundary.

## Migration Plan

1. Update generated projected-trait helpers to track touched registry components and previous durable values.
2. Change `project` statement emission to call the helper and write into the registry.
3. Change ordinary system emission back to `registry.view<filter...>(exclude...)` iteration.
4. Update trait access helpers and trait-match code to use normal registry access where overlay helpers are no longer needed.
5. Change frame cleanup to restore/remove projected registry components.
6. Update tests and examples to validate behavior rather than overlay-map internals.

## Open Questions

- Should `remove Trait` after `project Trait` in the same frame cancel restore, or should cleanup restore the pre-frame durable value regardless?
- Should no-filter systems continue scanning all live entities, and if so should generated filter guards use `continue`-style control rather than `return` anywhere they remain necessary?
- Is additional snapshotting needed when a handler projects a trait that is part of the same active view's filter?
