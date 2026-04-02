## Context

The existing `MatchExpr` in the AST is an **expression-level** match used for enum/value dispatch. This change introduces a **statement-level** `match` that operates on `entity_id` values, performing dynamic trait presence checks.

The `entity_id` type already exists in the type system as a primitive. Event declarations already support `entity_id` fields (`var other: entity_id`). The `on event as alias:` syntax is already in `EventHandlerNode`. All the building blocks are present — this change adds the dispatch mechanism on top of them.

The canonical usage:
```cactus
event Collision:
    var other: entity_id   # carried entity reference

system PlayerCombatSystem:
    filter:
        Health as h
    on collision as c:
        match c.other:             # subject is entity_id → trait-match mode
            Boss as b =>
                h.value -= b.contact_damage
                add Invincible(duration = 2.0)
            EnemyAI as e =>
                h.value -= e.base_damage
            Spike =>               # marker trait — no alias needed
                h.value -= 1
            _ =>
                pass
```

## Goals / Non-Goals

**Goals:**
- `match entity_id_expr:` dispatches to the first arm whose trait is present on the entity
- Arms use `TraitName [as alias] =>` syntax — alias binds to the trait data (read/write)
- Optional wildcard `_ =>` arm handles the no-match case
- No wildcard is required (open world, no-match = no-op)
- Compile-time validation: trait names must be declared; alias cannot shadow filter bindings
- Compiles to `if/else-if` chain of `registry.try_get<T>()` calls — zero overhead
- First matching arm wins and subsequent arms are skipped
- Trait match and value match reuse the `match` keyword, distinguished by subject type at semantic analysis time

**Non-Goals:**
- Multiple arms firing for the same entity (first-match-wins only)
- Matching on multiple traits simultaneously in a single arm (no `Boss, Shielded as s =>`)
- Exhaustiveness checking (trait sets are open; no compiler enforcement)
- Read-only enforcement on alias binding (same mutability rules as filter aliases)
- `match` on `entity_id` outside a system event handler

## Decisions

### Decision 1: Statement-level `match` distinct from expression-level `MatchExpr`

The existing `MatchExpr` is an expression (returns a value). Trait matching is a statement (runs a body, no return value). These are distinct AST nodes: `MatchExpr` for expressions, `TraitMatchStmt` for the new statement form.

At parse time, the parser can produce a `TraitMatchStmt` when it encounters `match IDENTIFIER:` at statement position. At semantic analysis, the type of the subject determines validation mode (entity_id → trait match, otherwise → value match for `MatchExpr`).

### Decision 2: Subject must be a direct `entity_id` expression

The `match` subject must evaluate to `entity_id`. This is validated semantically. Common cases:
- `match c.other:` where `c` is a collision event with `other: entity_id`
- `match target_id:` where `target_id` is a local `entity_id` variable

### Decision 3: Alias binds to trait data with full read/write access

`Boss as b =>` binds `b` to the `Boss` component data on the matched entity. This is mutable access — the same rules as filter aliases. Under the hood, `b` is a pointer/reference to the component storage.

### Decision 4: Arm order matters, first match wins

If an entity has both `Boss` and `EnemyAI`, only the arm that appears first in the source fires. This is explicit and predictable. Users control dispatch priority by arm ordering.

### Decision 5: Generated code uses `try_get`

```cpp
// match c.other:
if (auto* b = registry.try_get<Boss>(c.other)) {
    b->health -= damage;
    // ...
} else if (auto* e = registry.try_get<EnemyAI>(c.other)) {
    health->value -= e->base_damage;
} else if (registry.all_of<Spike>(c.other)) {    // marker trait — no data pointer needed
    health->value -= 1;
} else {
    // _ arm or nothing
}
```

Marker traits (no fields) use `all_of<T>` instead of `try_get<T>` since there's no data to bind.

### Decision 6: `_ =>` wildcard is optional

Trait matching is open-world. An entity may have none of the listed traits. When no arm matches and there is no `_ =>` arm, execution silently continues. This is the correct semantics for ECS collision handling — most collisions are irrelevant to most systems.

## Risks / Trade-offs

- **Parser ambiguity**: `match IDENTIFIER:` could be either a value match (if `IDENTIFIER` is an enum value) or a trait match (if `IDENTIFIER` is entity_id). Resolution: the parser always produces a `TraitMatchStmt` at statement level; the semantic analyzer validates the subject type and arms accordingly. If the subject is not `entity_id`, it falls back to value match validation (arms must be enum variant patterns).

- **Alias naming conflict**: If the match arm alias has the same name as a filter alias (`filter: Position as p` and arm `Boss as p`), there's a conflict. The semantic analyzer MUST report this as an error.

- **Nested match**: Trait match inside a trait match arm body is allowed since it's just a statement context. No special restriction needed.
