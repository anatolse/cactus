## Why

Cactus templates are useful spawnable blueprints, but reuse is still shallow. Real game content needs many small variants that share most traits and override only a few fields: walker enemies, flying enemies, gem variants, pickups, hazards, weapon variants, and platform archetypes.

Without template composition, authors duplicate entire trait blocks or create many nearly-identical units. This weakens the language's simple-but-powerful identity because content scale becomes verbose even when mechanics are clean.

## What Changes

- Add declarative template composition using archetype-body `use TemplateName` entries inside `template` and `unit` bodies.
- Reuse the existing `use` keyword contextually: top-level `use` remains module import, while indented archetype-body `use` composes a template blueprint.
- Define deterministic expansion and override semantics for used templates.
- Reject cyclic template-use graphs.
- Allow public templates from imported modules using existing qualified/aliased name resolution.
- Lower composed templates to the same flattened archetype representation already used by units, templates, and spawn sites.

Example direction:

```cactus
template EnemyBase:
    Health:
        health = 3
    Shape:
        color = #CC3333FF

template WalkerEnemy:
    use EnemyBase
    PatrolMotion:
        patrol_speed = 140.0

unit Walker1:
    use WalkerEnemy
    WorldTransform:
        position = vec2(400.0, 568.0)
```

## Capabilities

### New Capabilities
- `dsl-template-composition`: declarative archetype-body `use` composition and flattening of reusable template archetypes.

### Modified Capabilities
- `dsl-parser`: parse archetype-body `use TemplateName` entries in archetype bodies.
- `dsl-semantic-analysis`: resolve archetype-body template uses, validate cycles, and compute merged archetypes.
- `backend-cpp-entt`: generate composed templates and units from flattened archetypes.

## Impact

- Affects parser, AST, semantic analyzer, code generation, and tests.
- Additive syntax; existing templates and units continue to work.
- Provides a foundation for content placement because `place` can instantiate compact variants instead of repeating whole trait sets.
