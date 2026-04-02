## ADDED Requirements

### Requirement: Cross-entity operations guarded by validity check
The EnTT backend SHALL wrap all generated code that operates on a `entity_id` expression (other than `self`) with a `registry.valid(id)` guard. If the entity is not valid (stale handle), the operation is skipped with no side effects. This applies to:
- `add Trait to target_expr` → guarded `emplace_or_replace`
- `remove Trait from target_expr` → guarded `remove`
- `destroy target_expr` → guarded `destroy`

The guard is always generated; it is not optional. In debug builds, the backend MAY emit a trace message when a stale handle is encountered.

```cpp
// Generated for: add Stunned(duration = 3.0) to c_other
if (registry.valid(c_other)) {
    registry.emplace_or_replace<Stunned>(c_other, Stunned{.duration = 3.0f});
}
```

#### Scenario: add to target generates validity guard
- **WHEN** `add Trait to some_id` is compiled
- **THEN** the generated code wraps `emplace_or_replace<Trait>(some_id)` with `if (registry.valid(some_id))`

#### Scenario: remove from target generates validity guard
- **WHEN** `remove Trait from some_id` is compiled
- **THEN** the generated code wraps `registry.remove<Trait>(some_id)` with `if (registry.valid(some_id))`

#### Scenario: destroy target generates validity guard
- **WHEN** `destroy some_id` is compiled
- **THEN** the generated code wraps `registry.destroy(some_id)` with `if (registry.valid(some_id))`

#### Scenario: self operations do not require validity guard
- **WHEN** `add Trait` (no `to` clause) is compiled
- **THEN** no validity guard is generated — the current entity is always valid within its own handler

### Requirement: Targeted event dispatch guarded by validity check
When an event is emitted with a `to expr` clause, the generated dispatch code SHALL check `registry.valid(target)` before delivering the event. If the target entity is not valid, the event is silently dropped.

```cpp
// Generated for: emit PlayerDamaged(5, walker_id) to walker_id
if (registry.valid(walker_id)) {
    // dispatch PlayerDamaged to walker_id's handlers
}
```

#### Scenario: targeted emit to stale entity is dropped
- **WHEN** `emit SomeEvent(...) to dead_id` executes and `dead_id` is stale
- **THEN** the event is not delivered to any handler; no error occurs

### Requirement: `exists(entity_id)` compiles to `registry.valid()`
The `exists(expr)` built-in expression SHALL compile to `registry.valid(expr_value)` where `expr_value` is the resolved `entt::entity` value. The result is `bool`.

```cpp
// Generated for: if exists(f.target):
if (registry.valid(f_target)) { ... }
```

#### Scenario: exists compiles to registry.valid
- **WHEN** `exists(f.target)` is compiled
- **THEN** the generated code is `registry.valid(f->target)`

### Requirement: `match entity_id:` with stale handle generates early-exit guard
The generated if/else-if chain for `TraitMatchStmt` SHALL begin with a `registry.valid(subject)` check. If the entity is not valid, the entire if/else-if chain (including the `_ =>` wildcard) is skipped.

```cpp
// Generated for: match c_other:  Boss as b => ...  _ => ...
if (registry.valid(c_other)) {
    if (auto* b = registry.try_get<Boss>(c_other)) {
        // Boss arm
    } else {
        // wildcard arm
    }
}
// If !valid → nothing executes
```

#### Scenario: match on stale entity skips all arms
- **WHEN** `match c_other:` is compiled and `c_other` is stale at runtime
- **THEN** neither trait arms nor the wildcard arm execute
