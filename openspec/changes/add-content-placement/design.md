## Context

Current Cactus has three related concepts:

| Construct | Time | Purpose |
|---|---:|---|
| `unit` | module/scene load | declare one pre-existing ECS entity instance directly |
| `template` | declaration-time | declare a reusable entity blueprint |
| `spawn` | runtime handler execution | dynamically create an entity from a template |

Exploration showed that the missing authoring feature is not a fourth construct named `place`. Instead, the existing module-level instance construct should be named more directly and should support template initialization:

| Construct | Time | Purpose |
|---|---:|---|
| `entity Name:` | module/scene load | declare one pre-existing entity directly |
| `entity Name from Template:` | module/scene load | declare one pre-existing entity from a reusable template plus overrides |
| `template Name:` | declaration-time | declare a reusable entity blueprint |
| `spawn Template:` | runtime handler execution | dynamically create an entity from a template |

This keeps the declarative/imperative split clear:

```text
Declarative/load-time: entity, template, system, trait, asset, input
Imperative/runtime:   spawn, destroy, load, add, remove, emit
```

`entity` also describes invisible controllers and singleton-like state holders better than `unit`, while remaining accurate for visible game-world objects.

## Goals / Non-Goals

**Goals:**
- Rename the module-level ECS instance declaration from `unit` to `entity`.
- Reduce repeated entity boilerplate for authored scene content.
- Make template reuse visible in authored level files via `entity Name from Template:`.
- Preserve deterministic startup/load behavior.
- Reuse existing nested trait block override style.
- Keep runtime `spawn` distinct from load-time authored entities.

**Non-Goals:**
- Runtime spawning replacement; `spawn` remains the handler-time creation mechanism.
- A new `place` keyword.
- Grouped declarations such as `entities from Template:` in this change.
- Tilemap import, CSV/JSON data tables, or external editor integration in this change.
- Compact row/table syntax for many placements.
- Persistent stable save/editor IDs beyond the entity declaration name.
- A dedicated `resource` or `singleton` construct for controllers/state holders.

## Decisions

### Rename `unit` to `entity`

`unit` currently means “module-level ECS entity instance,” not only a visible actor or gameplay unit. `entity` is more explicit and works for players, enemies, props, cameras, lights, level directors, and singleton-like state holder entities.

Alternative considered: keep `unit`. This avoids migration churn but preserves a less accurate term before adding more load-time instance syntax.

### Add template-backed entity syntax

Syntax:

```ebnf
entity_decl = [ "pub" ] "entity" IDENTIFIER [ "from" dotted_name ] ":" NEWLINE INDENT
              { archetype_trait_entry }
              DEDENT ;
```

Examples:

```cactus
entity Walker1 from WalkerEnemy:
    WorldTransform:
        position = vec2(400.0, 568.0)
    PatrolMotion:
        patrol_min_x = 350.0
        patrol_max_x = 550.0

entity LevelDirector:
    LevelState:
        wave_index = 0
        alarm_active = false
```

A template-backed entity:

1. Resolves its template reference using normal module/import rules.
2. Starts from the referenced template's flattened archetype.
3. Applies nested trait override blocks using the same merge rules as `spawn` overrides.
4. Produces one load-time entity during module/world setup.
5. Has no expression value and cannot appear inside handlers.

Entity names are declaration names for diagnostics and generated-symbol stability. They are not automatically `entity_id` constants in this change, because exposing stable entity handles at top level would complicate scene reload and stale-handle semantics.

### Do not add `place`

`place Name from Template:` expresses level-authoring intent, but it adds a fourth construct where the smaller model is enough:

```text
template = blueprint
entity   = load-time entity instance, inline or from template
spawn    = runtime entity creation from template
```

### Defer grouped/template-table placement

Grouped syntax remains attractive:

```cactus
entities from BlueGem:
    GemA:
        WorldTransform:
            position = vec2(250.0, 560.0)
    GemB:
        WorldTransform:
            position = vec2(350.0, 410.0)
```

However, v1 should first establish the core template-backed entity semantics. Group defaults, typed native tables, and editor/export workflows should be separate changes after examples demonstrate the pain.

CSV is intentionally out of scope. Cactus-native syntax preserves typed expressions, enum/const/asset references, trait ownership, import resolution, comments, and diagnostics. External tools can generate `.cactus` or compiler data artifacts rather than forcing a stringly typed CSV schema into the core DSL.

### Ordering

Inline entities and template-backed entities instantiate deterministically in source declaration order within a module after imports are resolved. If module load order is already defined elsewhere, template-backed entities participate in the same module setup order as inline entities.

This means:

```cactus
entity A: ...
entity B from SomeTemplate: ...
entity C: ...
```

instantiates as `A`, then `B`, then `C` for backends where creation order is observable through `order by:` or deterministic setup diagnostics.

## Risks / Trade-offs

- **Breaking rename:** Existing `unit` code must migrate to `entity`. Mitigation: update examples/tests/docs together and provide clear migration wording.
- **Overlap with body-level `use`:** `entity Name from Template:` and `entity Name: use Template` can produce similar archetypes. Mitigation: document `from` as the preferred authored instance form and body-level `use` as compile-time template composition.
- **Future editor needs:** Entity declaration names may not be enough for editor round-tripping. Mitigation: avoid overcommitting to stable runtime handles now.
- **Ordering semantics:** Must be explicit so generated setup remains predictable.
- **No grouped syntax initially:** Repetition remains for many instances of the same template. Mitigation: defer `entities from Template:` until singular semantics are proven.

## Open Questions

- Should `pub entity` remain valid after the rename, and if so what does cross-module visibility mean for entity declarations?
- Should entity declaration names become stable editor IDs later?
- Should empty template-backed entity bodies be accepted for “use template defaults exactly”?
- Should a future `resource` or `singleton` construct exist for global state distinct from ECS entities?
