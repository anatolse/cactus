## ADDED Requirements

### Requirement: Code generation for `TraitMatchStmt`
The EnTT backend SHALL generate an `if/else-if` chain for `TraitMatchStmt` nodes. Each `TraitMatchArm` compiles to a conditional block testing whether the entity has the trait, with an optional bound pointer for traits with fields. Marker traits (no fields) use `registry.all_of<T>()`. The wildcard arm `_ =>` compiles to the final `else` block.

Generated pattern for:
```cactus
match c.other:
    Boss as b =>
        b.phase += 1
    EnemyAI as e =>
        h.value -= e.base_damage
    Spike =>
        h.value -= 1
    _ =>
        pass
```

Generates:
```cpp
if (auto* b = registry.try_get<Boss>(c_other)) {
    b->phase += 1;
} else if (auto* e = registry.try_get<EnemyAI>(c_other)) {
    health->value -= e->base_damage;
} else if (registry.all_of<Spike>(c_other)) {
    health->value -= 1;
} else {
    // wildcard (or omitted if no _ arm)
}
```

Where `c_other` is the resolved `entity_id` value from the subject expression.

#### Scenario: Trait arm with fields generates try_get pointer
- **WHEN** `Boss as b =>` arm is compiled and `Boss` has fields
- **THEN** the generated code is `if (auto* b = registry.try_get<Boss>(subject_entity)) { ... }`

#### Scenario: Marker trait arm generates all_of check
- **WHEN** `Spike =>` arm is compiled and `Spike` is a marker trait
- **THEN** the generated code is `} else if (registry.all_of<Spike>(subject_entity)) { ... }`

#### Scenario: Wildcard arm generates else block
- **WHEN** `_ =>` arm is compiled
- **THEN** the generated code is a bare `else { ... }` block

#### Scenario: No wildcard arm generates no else block
- **WHEN** the match has no `_ =>` arm
- **THEN** the generated if/else-if chain has no final `else` clause; no-match is a silent no-op

#### Scenario: Subject expression evaluated once
- **WHEN** `match c.other:` is compiled
- **THEN** `c.other` (the entity_id expression) is evaluated once and stored in a local variable used for all `try_get`/`all_of` calls
