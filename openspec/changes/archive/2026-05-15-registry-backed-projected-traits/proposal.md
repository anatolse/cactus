## Why

The projected-traits implementation currently stores projected values in separate overlay maps and compensates by making generated systems scan `registry.storage<entt::entity>()` with manual projected-or-durable filter checks. That broke system filtering: the generated filter guard uses `return`, so the first non-matching entity exits the entire handler instead of continuing through all entities.

The previous durable-only system iteration based on `registry.view<...>()` already had the right filtering behavior. Projected traits should integrate with that path instead of replacing it with a whole-registry scan.

## What Changes

- Make projected traits materialize as real EnTT registry components during the frame.
- Track which projected components were added or temporarily replaced so frame cleanup can remove projected-only components and restore pre-existing durable components.
- Return ordinary generated system filtering to `registry.view<...>()` and native EnTT exclusion where possible.
- Remove the separate projected-overlay maps as the source of truth for system matching.
- Preserve current author-facing `project` semantics: projected facts are visible to later systems in the same frame, coalesced per `(entity, trait)`, and cleared at the frame boundary.

## Capabilities

### Modified Capabilities
- `dsl-projected-traits`: clarify that projected traits are frame-local facts, not necessarily separate storage from the backend registry.
- `backend-cpp-entt`: implement projected traits as registry-backed transient components and restore robust view-based filtering.

## Impact

- Affects cpp-entt code generation for ordinary systems, `project` statements, projected-trait helper emission, and frame-boundary cleanup.
- Requires backend tests to stop expecting prefixed projected-overlay maps and `registry.storage<entt::entity>()` filtering for ordinary systems.
- New generated helper identifiers for projected-trait tracking SHALL avoid the `cactus` prefix.
- Fixes the early-exit filtering regression while keeping projected traits visible to `filter:`, `exclude:`, trait matching, and backend-owned systems through normal EnTT component access.
