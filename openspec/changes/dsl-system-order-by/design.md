## Context

Entity iteration order in ECS systems is undefined by default — it follows internal storage layout, which changes as entities are added/removed. For most logic systems this is fine, but rendering requires deterministic order: layer 0 before layer 1, and within a layer, lower Y-position entities drawn first (Y-sort for depth illusion). Forcing users to manually sort outside the DSL, or to abuse the existing phase system, is error-prone and inexpressive.

The `order by:` clause adds a per-system declarative sort that runs before each handler invocation. It maps directly to EnTT's `registry.sort<T>(comparator)` API, which performs an in-place sort of the component pool and provides fast incremental re-sorting.

Note: `SystemNode` already has `after_systems: vector<string>` in the AST for system-to-system execution ordering. This change is exclusively about entity ordering *within* a system — the two are orthogonal.

## Goals / Non-Goals

**Goals:**
- Declarative per-system entity sort with `order by: alias.field [asc|desc]`
- Multi-key lexicographic sort (primary key first, tie-breaking by subsequent keys)
- Compile-time validation: sort keys must be from filter traits, must be scalar-comparable types
- `asc` is the default direction when omitted
- Applies to all handlers in the system (one sort per handler invocation)
- EnTT backend generates `registry.sort<T>(comparator)` before the view iteration

**Non-Goals:**
- Per-handler sort direction (one `order by:` per system, not per handler)
- Sorting by arbitrary computed expressions (only `alias.field` form)
- Stable-sort guarantees (implementation-defined stability)
- `order by:` on systems without a `filter:` (no alias bindings to reference)
- Runtime-dynamic sort keys

## Decisions

### Decision 1: Sort key form restricted to `alias.field`

Sort keys must be `alias.field` where `alias` is declared in the system's `filter:` block. This means:
- The sort key is always a direct field access on a filtered trait
- The compiler can validate the alias, field name, and field type statically
- No arbitrary expressions (no `p.pos.x + p.pos.y`, no function calls)

**Alternative considered**: Allow arbitrary expressions. Rejected because arbitrary expressions require evaluating a lambda per entity pair, which is harder to type-check, may have side effects, and complicates codegen. Field access is the 95% case and is zero-ambiguity.

For `vec2`/`vec3` fields, the user must access a scalar component: `p.pos.y` (float, valid) rather than `p.pos` (vec2, invalid). The compiler validates this.

### Decision 2: `order by:` is a system-level clause, not per-handler

A single `order by:` applies to all handlers in the system. This keeps the system declaration compact and makes the sort intent obvious at the system level.

**Alternative considered**: Per-handler sort: `on tick(dt: float) order by s.layer asc:`. Rejected for verbosity and because systems with multiple handlers (e.g., `on tick` + `on late_tick`) virtually always want the same sort.

### Decision 3: `registry.sort<T>()` called on the primary sort key's component type

EnTT sorts the component pool of type `T`, then iteration follows that sort order. For multi-key sort with keys from different components, the sort is done on the first key's component type, and the comparator accesses other components via `registry.get<T2>(entity)`.

```cpp
// Generated for: order by: s.layer asc, p.pos.y desc
registry.sort<Sprite>([&](const entt::entity a, const entt::entity b) {
    const auto& sa = registry.get<Sprite>(a);
    const auto& sb = registry.get<Sprite>(b);
    if (sa.layer != sb.layer) return sa.layer < sb.layer;
    const auto& pa = registry.get<Position>(a);
    const auto& pb = registry.get<Position>(b);
    return pa.pos.y > pb.pos.y;
});
```

This is a single comparator, single sort call, single pass. No separate per-key sort passes.

### Decision 4: Sort runs every frame per handler

Sorting is called before each invocation of each handler in the system, every frame. EnTT's sort is incremental for nearly-sorted data (common when few entities change position each frame), so re-sorting each frame is low cost in practice.

**Alternative considered**: Sort only when data changes (reactive sort). Rejected because detecting "data changed" requires dirty flags or change detection infrastructure not currently in the DSL.

## Risks / Trade-offs

- **Sort cost on large entity sets**: Sorting N entities is O(N log N). For small-to-medium entity counts (< 10,000) typical in game scenes, this is negligible. Document that `order by:` is not suitable for systems processing hundreds of thousands of entities per frame.

- **EnTT view ordering caveat**: EnTT's `sort<T>()` sorts the sparse set for component T. The view iteration order then follows that sorted pool. For multi-component views, iteration follows the "smallest component pool first" heuristic by default, but when sorting is applied to a specific component, that component's order is respected.

- **Manual backend**: The cpp-manual backend does not use EnTT. The sort will need to be implemented as a `std::sort` on the SoA index array for that backend.
