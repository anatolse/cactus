## Context

The current trait toggle system (`enable`/`disable`) was designed for a closed-world model where every entity's component set is fixed at spawn time via `apply:`. The `: disabled` annotation pre-allocates component storage but starts it inactive. This model works well for toggling known components but cannot express truly dynamic composition — attaching a component type that was never in the entity's `apply:` block.

The `add`/`remove` replacement makes the entity component set open at runtime, matching how ECS backends like EnTT naturally operate (structural changes via `emplace`, `remove`). No backward compatibility is required.

Key existing pieces that shape this design:
- `FieldNode` already has `default_value: optional<ExprNode>` in the AST — default values are already parseable
- `SpawnArg` already defines named-arg initialization (`name`, `key_prefix`, `value`) — `add` reuses this pattern exactly
- `entity_id` is already a primitive type — cross-entity targeting is naturally expressible
- `EnableStmt` / `DisableStmt` exist in the AST and are the primary removal targets
- `ApplyEntry.initially_active` exists solely for `: disabled` and will be removed

## Goals / Non-Goals

**Goals:**
- Replace `enable`/`disable` with `add`/`remove` as the sole runtime trait management mechanism
- `add` uses emplace-or-replace semantics (idempotent, safe to call repeatedly)
- `remove` destroys the component and all its field data
- Trait field declarations support default values — traits where all fields have defaults can be `add`ed bare (no args)
- Cross-entity targeting: `add Trait to entity_id` / `remove Trait from entity_id`
- `apply:` simplified — no `: disabled` annotation; it becomes a plain spawn-time add list
- `exclude:` + marker traits replaces the "disable to hide from filter" pattern
- All changes are breaking; no migration compatibility layer

**Non-Goals:**
- Preserving data across `remove` (data is destroyed on remove)
- Partial field updates on existing components (add patches all supplied fields, leaves others as-is)
- Deferred/batched structural changes (structural changes take effect immediately)
- Enabling/disabling multiple traits in one statement

## Decisions

### Decision 1: `add` uses emplace-or-replace semantics

`add Frozen(duration = 5.0)` on an entity that already has `Frozen` will overwrite the `duration` field with the new value (and leave other fields untouched). This maps to EnTT's `emplace_or_replace<T>(entity, ...)`.

**Alternative considered**: Error on duplicate add. Rejected because the most common use case — refreshing a status effect timer — requires idempotent re-add. Forcing the caller to `remove` then `add` is verbose and introduces a frame where the entity lacks the component.

**Alternative considered**: No-op if present. Rejected because it prevents refreshing fields, which is the primary reason to re-add.

### Decision 2: Named args matching `spawn` pattern

`add Health(current = 100, max = 100)` uses the same named-arg syntax as `spawn Enemy(pos = vec2(0,0))`. This reuses the existing `SpawnArg` AST structure and keeps the language consistent.

Fields are matched by name. Field order does not matter. If a field has a default value, it may be omitted. If a field has no default, it must be supplied — this is a compile-time error.

### Decision 3: Cross-entity targeting via `to` / `from` prepositions

`add Stunned(duration = 2.0) to other_id` — reads naturally. `remove Shield from parent_id`.

The target expression must evaluate to `entity_id`. This is validated at semantic analysis time. When no `to`/`from` is given, the target is implicitly `self` (the current entity being processed by the handler).

**Risk**: Structural changes to other entities while iterating over a view. EnTT's views are generally stable against adds/removes to entities other than the one currently being iterated. Document this as a known constraint; the DSL does not need to model iteration stability explicitly.

### Decision 4: `apply:` becomes a plain trait list

The `ApplyEntry.initially_active` field and the `: disabled` parser production are removed. `apply:` now simply declares which traits are added when the entity spawns, with values drawn from the `config:` block. This matches the semantics of calling `add TraitName(config fields...)` at spawn time.

### Decision 5: `enable`/`disable` replaced by marker traits + `exclude:`

The `exclude:` block already exists and filters out entities that have a given trait. The pattern for "temporarily making an entity invisible to a system" is:

```cactus
trait Invincible       # marker — zero cost, no fields

system DamageSystem:
    filter:   Health as h
    exclude:  Invincible
    on collision as c:
        h.value -= c.damage
```

`add Invincible` / `remove Invincible` replaces `enable`/`disable` for this pattern.

This is more expressive — a single marker trait can be `exclude:`d by any number of systems independently.

## Risks / Trade-offs

- **Data loss on remove**: `remove TraitName` permanently destroys field data. Users who want the "toggle while preserving data" pattern must keep the data in a separate trait and use a marker for filtering. This is more explicit and avoids hidden state, but requires understanding the pattern.

- **No compile-time proof of component presence**: With open-world composition, the compiler cannot guarantee that an entity has a given trait at any program point. `filter:` entries are runtime queries. This means some classes of bugs (accessing a trait that's been removed) become runtime rather than compile-time errors. Mitigation: the compiler still validates that all trait names in `add`/`remove`/`filter:`/`exclude:` refer to declared traits.

- **Iteration invalidation**: Structural changes (`add`/`remove`) while iterating a view can invalidate the view in some ECS implementations. EnTT handles this gracefully for the common cases (adds/removes on entities not currently being iterated). Document as a best-practice constraint rather than a compiler enforcement.

- **`add` patches only supplied fields**: If a trait has fields `{a, b, c}` and you `add T(a = 1)` on an existing instance, only `a` is updated. This may surprise users expecting all-or-nothing replacement. The semantics follow `emplace_or_replace` naturally.

## Migration Plan

1. Remove `EnableStmt`, `DisableStmt` from `ast.h`
2. Remove `ApplyEntry.initially_active` from `ast.h`
3. Add `AddTraitStmt`, `RemoveTraitStmt` to `ast.h`
4. Update lexer: remove `enable`/`disable` keywords; add `add`, `remove` as keywords
5. Update parser: remove `enable_stmt`/`disable_stmt` productions; add `add_stmt`/`remove_stmt`
6. Update semantic analyzer: remove enable/disable checks; add add/remove validation
7. Update both backends (cpp-entt, cpp-manual): generate `emplace_or_replace`/`remove` calls
8. Migrate all example files and test fixtures
9. Update test suites

No runtime migration or data format changes — this is a DSL-level change only.
