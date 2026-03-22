## Context

The Cactus DSL uses an ECS architecture where **traits = data** and **systems = logic**. The current AST and grammar have a contradictory artifact: `TraitNode` holds a `handlers` vector and the grammar allows `event_handler | func_decl` inside trait bodies. This path is completely unused — all real code (stdlib, examples, tests) keeps traits as pure data. Additionally, system execution order is only partially deterministic: within a module systems run in declaration order, but there is no way to express ordering constraints between systems that have no shared trait dependency (the canonical case being render pass layering).

This change is frontend-only. Backends receive a `DecoratedProgram` whose `SystemInfo` already carries the ordering graph — adding explicit `after_systems` to `SystemInfo` is sufficient for backends to consume without any emitter logic changes.

## Goals / Non-Goals

**Goals:**
- Remove `handlers` from `TraitNode` in the AST and close the corresponding parser path
- Add `after:` clause to `SystemNode` in AST and parse it
- Validate `after:` references (all named systems must exist) and detect ordering cycles in the semantic analyzer
- Store explicit `after_systems` edges in the `DecoratedProgram` dependency graph
- Update `spec/cactus_dsl_spec.md` to match the new grammar and semantics

**Non-Goals:**
- Auto-inferring read/write dependency from handler body assignments (separate future change)
- Any backend code changes
- Parallel scheduling of independent systems (runtime concern, not compiler)
- Cross-module `after:` ordering (systems in other modules cannot be referenced by name today)

## Decisions

### Decision: `after:` uses block syntax, consistent with `filter:` and `exclude:`

```cactus
system UIRenderSystem:
    filter:
        UIRenderer as ui
    after:
        SceneRenderSystem
        BackgroundRenderSystem
    on tick(dt: float):
        ...
```

**Rationale:** Block syntax is consistent with all other clause types in a system declaration (`filter:`, `exclude:`). The parser already handles INDENT/DEDENT for these clauses, so there is no added complexity. Consistency lowers the learning curve.

### Decision: `after:` resolves only systems in the same compilation unit (same linked program)

Cross-module `after:` is not supported in this change. The semantic analyzer validates that every name in `after:` resolves to a `SystemNode` in the current `DecoratedProgram` (which includes all linked modules). This means `after:` works fine across modules as long as both are compiled and linked together — but the system is referenced by its simple name, not qualified (`after: player.MovementSystem` is not supported; just `after: MovementSystem`).

**Rationale:** Qualified name resolution adds complexity and the use cases (render passes, scene ordering) are almost always within the same game module. Can be extended later.

### Decision: Cycle detection via DFS on the explicit `after:` graph

The semantic analyzer builds the full system execution graph (implicit order from declaration + explicit `after:` edges) and runs a DFS cycle check. A cycle in `after:` constraints is a compile error.

**Rationale:** Cycle detection is cheap at compile time and prevents confusing runtime behaviour. The DFS is O(V+E) and the number of systems is small.

### Decision: Config/spawn field qualification — optional dotted form, TraitName as implicit alias

```cactus
# Current (bare names):
unit Player:
    apply:
        Position
        Health
    config:
        position = vec3(0.0, 0.0, 0.0)   # bare — valid if unambiguous
        health = START_HEALTH

# New (qualified — explicit when desired or required by ambiguity):
unit Player:
    apply:
        Position as pos     # optional alias declared
        Health              # no alias — TraitName is implicit alias
    config:
        pos.position = vec3(0.0, 0.0, 0.0)   # alias-qualified
        Health.health = START_HEALTH          # TraitName-qualified

# Spawn overrides — same rules:
let e = spawn Enemy(EnemyAI.patrol_speed = 5.0, Position.pos = vec2(400.0, 568.0))
```

**Rules:**
1. A `config:` key or `spawn` override arg may be a bare `IDENTIFIER` or a dotted `IDENTIFIER.IDENTIFIER` (trait/alias prefix + field name).
2. Bare keys are valid when the name matches exactly one field across all applied traits. Ambiguous bare keys are a semantic error.
3. Dotted keys resolve the first component against declared `apply:` entries (by trait name or alias). If the first component matches an alias, the field is looked up in the aliased trait. If it matches a trait name directly (implicit alias), same resolution. Unknown first component → error.
4. `apply:` optionally supports `as alias` (same `FilterEntry` pattern already used by `filter:`). This is recorded in `ApplyEntry.alias`.
5. No change to how bare names work today — existing `.cactus` files are backward compatible as long as field names are unique across applied traits.

**Rationale:** The implicit alias pattern (trait name IS the alias) is already the mental model users have from system `filter:`. Extending it to `apply:` / `config:` is symmetric and requires no new concepts.

## Risks / Trade-offs

- **Breaking change for trait handlers** — Any `.cactus` file that placed an `on ...` or `func` block inside a trait body will now fail to parse. In practice this is zero known files (the feature was grammatically present but unused), but it is technically breaking.  
  → **Mitigation:** The error message should be clear: *"event handlers are not allowed in traits; declare a system instead."*

- **`after:` by name is fragile to renames** — If a system is renamed, all `after: OldName` references become stale errors.  
  → **Mitigation:** This is the same tradeoff as filter trait names. The compiler gives a clear "unknown system" error. Acceptable at this stage.

- **`spec/cactus_dsl_spec.md` is a monolith** — Editing it risks drift from the OpenSpec delta specs. The monolith and the delta specs must be kept in sync manually.  
  → **Mitigation:** Tasks explicitly list both the delta spec and the monolith update as separate checklist items.
