## Why

Cactus can express gameplay mechanics, but large authored content still requires too much repetition. Current examples model every placed platform, gem, enemy, or prop as a separate `unit` with repeated trait blocks.

Real games need a distinction between reusable entity blueprints and authored instances placed in a scene or level. `template` already covers the blueprint side. Cactus needs a declarative load-time placement facility for instances.

## What Changes

- Add a top-level `place Name from TemplateName:` declaration for load-time instantiation of a template with per-instance trait overrides.
- Define `place` as declarative content, not a runtime statement and not an expression.
- Make placed entities instantiate alongside `unit` declarations during module/scene load.
- Reuse existing nested trait override syntax from `spawn` and archetype declarations.
- Define deterministic initialization order for units and placements.

Example direction:

```cactus
template BlueGem:
    Shape:
        size = vec2(16.0, 16.0)
        color = #4488FFFF
    Collectible:
        point_value = 10

place BlueGem_250_560 from BlueGem:
    WorldTransform:
        position = vec2(250.0, 560.0)

place BlueGem_350_410 from BlueGem:
    WorldTransform:
        position = vec2(350.0, 410.0)
```

## Capabilities

### New Capabilities
- `dsl-content-placement`: declarative load-time placement of template instances.

### Modified Capabilities
- `dsl-parser`: parse top-level `place` declarations.
- `dsl-semantic-analysis`: resolve placement templates and validate override trait entries.
- `backend-cpp-entt`: instantiate placements during generated module/world setup.
- `example-cpp-compilation-tests`: supported examples can use placement to reduce repeated unit boilerplate once implemented.

## Impact

- Affects parser, AST, semantic analyzer, backend setup generation, and tests.
- Additive syntax; existing `unit`, `template`, and `spawn` behavior remains valid.
- Complements template composition but can be implemented independently if placements instantiate ordinary templates first.
