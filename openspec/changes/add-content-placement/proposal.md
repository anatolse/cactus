## Why

Cactus can express gameplay mechanics, but authored scene content still requires too much repetition. Current examples model every placed platform, gem, enemy, prop, controller, or singleton as a separate `unit` with repeated trait blocks.

The language also uses `unit` for module-level ECS instances even when they represent invisible controllers or singleton state. `entity` is a clearer name for the construct Cactus actually provides: a module/scene-load entity instance. Once that naming is explicit, reusable authored content can be modeled as an `entity` initialized from a `template` rather than as a separate `place` construct.

## What Changes

- **BREAKING**: Rename top-level `unit` declarations to `entity` declarations.
- Add `entity Name from TemplateName:` declarations for load-time entities initialized from reusable templates with per-entity nested trait overrides.
- Define `entity Name from TemplateName:` as declarative load-time content, not a runtime statement and not an expression.
- Make template-backed entities instantiate alongside inline entities during module/scene load.
- Reuse existing nested trait override syntax from `spawn` and archetype declarations.
- Preserve deterministic initialization order for inline entities and template-backed entities.
- Do not add a separate `place` keyword in this change.
- Defer grouped declarations such as `entities from TemplateName:` and table/CSV placement import to future changes.

Example direction:

```cactus
template BlueGem:
    Shape:
        size = vec2(16.0, 16.0)
        color = #4488FFFF
    Collectible:
        point_value = 10

entity BlueGem_250_560 from BlueGem:
    WorldTransform:
        position = vec2(250.0, 560.0)

entity BlueGem_350_410 from BlueGem:
    WorldTransform:
        position = vec2(350.0, 410.0)
```

## Capabilities

### New Capabilities
- `dsl-template-backed-entities`: declarative module/scene-load entities initialized from templates with per-entity overrides.

### Modified Capabilities
- `dsl-lexer`: recognize `entity` as the top-level entity declaration keyword and reserve it in place of `unit`.
- `dsl-parser`: parse `entity` declarations, including `entity Name from TemplateName:` template-backed declarations, and remove `unit` from the active top-level declaration grammar.
- `dsl-semantic-analysis`: validate inline entity declarations and template-backed entity declarations, resolve template references, and validate override trait entries.
- `dsl-templates`: update template terminology from `unit` to `entity` and distinguish template-backed entities from runtime `spawn`.
- `dsl-scene-loading`: instantiate inline and template-backed `entity` declarations during module/scene load.
- `backend-cpp-entt`: instantiate template-backed entities during generated module/world setup and update generated setup terminology from units to entities.
- `example-cpp-compilation-tests`: supported examples can use `entity Name from TemplateName:` to reduce repeated entity boilerplate once implemented.

## Impact

- Affects lexer, parser, AST, semantic analyzer, module artifacts/data files, backend setup generation, examples, docs, and tests.
- Existing `unit` examples and fixtures must migrate to `entity`.
- Existing `template` and runtime `spawn` behavior remains conceptually valid; `spawn` stays the handler-time creation mechanism.
- Template-backed entities complement template composition but provide clearer authored instance intent than body-level `use TemplateName` inside an entity.
