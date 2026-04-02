## Why

The current spec makes an untrue runtime promise: *"An `entity_id` value SHALL always refer to a live entity."* This is false. Entities can be destroyed at any time. A stored `entity_id` field in a trait can become stale — pointing to a destroyed entity — and nothing in the language or runtime prevents that. Making this promise in the spec creates a false safety illusion and prevents the language from defining what actually happens when a stale handle is used.

The fix is straightforward: reframe `entity_id` as an opaque handle, define **total operation semantics** (all operations on stale/dead handles are safe no-ops or no-match), and add `exists(entity_id)` for the rare cases where authors need to test handle validity. This matches the stated goal of the language: *authors describe mechanics; the backend is responsible for safety*.

## What Changes

- **BREAKING**: Remove the "always refers to a live entity" guarantee from the `entity_id` spec
- Redefine `entity_id` as an opaque entity handle — no null, but handles may become stale when the referenced entity is destroyed
- Define **total operation semantics** for stale handles (all operations are safe):
  - `add Trait to stale_id` → no-op
  - `remove Trait from stale_id` → no-op
  - `emit Event(...) to stale_id` → dropped (event not delivered)
  - `destroy stale_id` → no-op
  - `match stale_id:` → no arm matches (falls through or wildcard)
- Add `exists(entity_id)` built-in expression that returns `bool` — `true` if the entity is still alive
- Remove the error message "entity_id has no null value; use trait enable/disable to model absent relationships" — with dynamic add/remove, absence is now modeled differently
- Update error for `entity_id == 0` to reference the new model: "entity_id has no null literal; use `exists(id)` to test handle validity"

## Capabilities

### New Capabilities
- `dsl-entity-id-total-semantics`: Opaque handle model, total operation semantics, `exists(entity_id)` expression

### Modified Capabilities
- `dsl-type-system`: Reframe `entity_id` semantics section; remove "always live" claim; add `exists()` built-in
- `dsl-dynamic-traits`: Add note that `add/remove ... to/from stale_id` is a runtime no-op
- `dsl-trait-pattern-match`: Add note that `match stale_id:` yields no match on any arm
- `backend-cpp-entt`: Generated code for cross-entity operations uses `registry.valid(id)` guard before acting; targeted event dispatch checks validity before delivery

## Impact

- `openspec/specs/dsl-type-system/spec.md`: Rewrite `entity_id` semantics paragraph
- `src/frontend/semantic_analyzer.cpp`: Update error message for `entity_id == 0` comparison
- `src/backends/cpp-entt/system_emitter.cpp`: Wrap cross-entity `add`/`remove`/`destroy` calls with `if (registry.valid(id))` guard
- `src/backends/cpp-entt/event_emitter.cpp`: Wrap targeted event dispatch with validity check
- No parser changes required
