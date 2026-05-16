## Context

The current `template` construct declares a reusable blueprint for runtime `spawn`, and `unit` declares an automatically-instantiated entity. Both use nested trait entries. This is clean but does not support sharing trait groups across archetypes.

In examples such as the platformer, repeated platforms, enemies, and gems duplicate most of their trait blocks. Content placement can reduce per-instance boilerplate, but template composition is the reusable blueprint layer underneath it.

## Goals / Non-Goals

**Goals:**
- Let authors compose templates from other templates without adding general inheritance or scripting.
- Support variants by using a base template and overriding selected trait fields locally.
- Keep expansion deterministic and easy to reason about.
- Reuse existing module import and public symbol rules.
- Avoid exposing backend-specific archetype or EnTT concepts.

**Non-Goals:**
- Multiple runtime inheritance or polymorphism.
- Trait inheritance.
- Arbitrary macros or compile-time programming.
- Conditional template uses.
- Template parameters/generics in the first version.

## Decision: Reuse `use TemplateName` entries instead of adding `include` or `extends`

The proposed first version uses body-level archetype `use` entries:

```cactus
template WalkerEnemy:
    use EnemyBase
    use GroundWalkerPhysics
    PatrolMotion:
        patrol_speed = 140.0
```

This is deliberately composition-oriented. It avoids implying a class hierarchy and lets authors combine multiple reusable fragments.

Reusing `use` avoids adding a new keyword while keeping the meaning close to “bring this declared thing into the current scope/body.” The meaning is context-sensitive but syntactically unambiguous:

- top-level `use std.physics.flat as phys` imports a module,
- archetype-body `use EnemyBase` composes a template into a `template` or `unit` body.

`extends` may feel familiar, but it suggests single-inheritance semantics and raises questions about base constructors, override keywords, and subtype relationships. Cactus templates are data blueprints, not types with polymorphic dispatch, so archetype-body `use` better matches the ECS model.

## Syntax Shape

An archetype body entry becomes either:

```text
archetype_entry = template_use_entry | archetype_trait_entry
template_use_entry = "use" dotted_name NEWLINE
```

Archetype-body `use` entries are valid inside:

- `template` bodies,
- `unit` bodies,
- `spawn` override bodies only if the spawned template is known at compile time and the template use is treated as an additional override fragment.

The first implementation may choose to support only `template` and `unit` bodies. Supporting archetype-body `use` in `spawn` override bodies is useful but not required for the core composition value.

## Expansion Semantics

Composition is a compile-time archetype-flattening operation.

Conceptually:

```text
template WalkerEnemy:
    use EnemyBase
    PatrolMotion: ...

        expands to

template WalkerEnemy:
    <all trait entries from EnemyBase>
    PatrolMotion: ...
```

Rules:

1. Archetype-body `use` entries are expanded in declaration order.
2. Local trait entries are applied after earlier template uses.
3. If the same trait appears more than once, the entries are merged field-by-field.
4. Later assignments override earlier assignments for the same field.
5. Marker-trait duplicates collapse to one marker entry.
6. A field not mentioned by a later override keeps the earlier value or the trait default.
7. After expansion, every applied data trait must satisfy existing initialization rules.

Example:

```cactus
template EnemyBase:
    Health:
        health = 3
        lives = 1

template BossEnemy:
    use EnemyBase
    Health:
        health = 50
```

Final `BossEnemy.Health` has `health = 50` and `lives = 1`.

## Validation Rules

- Archetype-body `use` names must resolve to templates, not modules, traits, units, structs, or events.
- Non-public templates from imported modules are not usable through archetype-body `use`.
- Template-use graphs must be acyclic.
- Ambiguous unqualified template names produce the same style of error as other ambiguous symbols.
- Duplicate field assignments in the same trait block remain errors if that is the current rule; overrides are only between separate entries after archetype-use expansion.

## Backend Model

Backends should not need special runtime support. Semantic analysis can produce flattened archetypes for:

- composed templates,
- units that use templates,
- spawn sites that instantiate composed templates.

The cpp-entt backend then emits the same component construction code it already emits for ordinary archetypes.

## Risks / Trade-offs

- **Override confusion:** Authors may not realize a later template use overrides an earlier template use. Mitigation: deterministic ordering and diagnostics for suspicious duplicate overrides can be added later.
- **Use cycles:** Must be rejected clearly.
- **Keyword overload:** `use` now has two meanings. Mitigation: keep meanings context-separated and document “top-level use = module import; archetype-body use = template composition.”
- **Too much implicitness:** Flattening hides where a field came from. Mitigation: diagnostics should include the template-use chain where possible.
- **Interaction with future placement:** `place` should instantiate templates after composition expansion rather than inventing a second merge model.

## Open Questions

- Should archetype-body `use` be allowed inside `spawn` override bodies in v1?
- Should overriding a `let` field from a used template require explicit syntax, or is construction-time override sufficient?
- Should diagnostics warn when two used templates provide the same trait and the later one overrides fields?
