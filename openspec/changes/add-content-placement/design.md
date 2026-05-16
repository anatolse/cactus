## Context

Current Cactus has three related concepts:

| Construct | Time | Purpose |
|---|---:|---|
| `unit` | module/scene load | declare one pre-existing entity directly |
| `template` | declaration-time | declare a reusable blueprint |
| `spawn` | runtime handler execution | dynamically create an entity from a template |

What is missing is a load-time instance of a template:

| Proposed construct | Time | Purpose |
|---|---:|---|
| `place` | module/scene load | create one authored instance from a template with overrides |

This is the level-authoring layer. It lets examples and games say “put this kind of thing here” without declaring a new entity archetype from scratch each time.

## Goals / Non-Goals

**Goals:**
- Reduce repeated `unit` boilerplate for placed content.
- Make template reuse visible in authored level files.
- Preserve deterministic startup behavior.
- Reuse existing nested trait block override style.
- Keep placement declarative and non-imperative.

**Non-Goals:**
- Runtime spawning replacement; `spawn` remains the handler-time creation mechanism.
- Tilemap import, CSV/JSON data tables, or external editor integration in v1.
- Compact row/table syntax such as `place_many` in v1 unless separately accepted.
- Persistent stable save IDs beyond the placement declaration name.

## Syntax Shape

Top-level placement declaration:

```ebnf
place_decl = "place" IDENTIFIER "from" dotted_name ":" NEWLINE INDENT
             { archetype_trait_entry }
             DEDENT ;
```

Examples:

```cactus
place Walker1 from WalkerEnemy:
    WorldTransform:
        position = vec2(400.0, 568.0)
    PatrolMotion:
        patrol_min_x = 350.0
        patrol_max_x = 550.0

place Ground from SolidPlatform:
    WorldTransform:
        position = vec2(0.0, GROUND_Y)
    Shape:
        size = vec2(GROUND_WIDTH, GROUND_HEIGHT)
```

The body may be empty in a later grammar variant, but v1 should require a colon body to keep parsing simple and make overrides explicit.

## Semantics

A `place` declaration:

1. Resolves its template reference using normal module/import rules.
2. Starts from the referenced template's flattened archetype.
3. Applies nested trait override blocks using the same merge rules as `spawn` overrides.
4. Produces one load-time entity during module/world setup.
5. Has no expression value and cannot appear inside handlers.

Placement names are declaration names for diagnostics and generated-symbol stability. They are not automatically `entity_id` constants in v1, because exposing stable entity handles at top level would complicate scene reload and stale-handle semantics.

## Ordering

Units and placements instantiate deterministically in source declaration order within a module after imports are resolved. If module load order is already defined elsewhere, placements participate in the same module setup order as units.

This means:

```cactus
unit A: ...
place B from SomeTemplate: ...
unit C: ...
```

instantiates as `A`, then `B`, then `C` for backends where creation order is observable through `order by:` or deterministic setup diagnostics.

## Why not only `unit use Template`?

Template composition can let authors write:

```cactus
unit Walker1:
    use WalkerEnemy
    WorldTransform:
        position = vec2(400.0, 568.0)
```

That helps, but it still conceptually declares a new archetype-like entity. `place` communicates a different authoring intent: this is level content, an instance of a blueprint.

The distinction becomes important for future editor tooling, level manifests, data import, and possible placement metadata.

## Deferred: `place_many` and data tables

Bulk placement is important, but v1 should first establish the semantic boundary with one simple declaration.

Future directions could include:

```cactus
place_many from BlueGem:
    GemA:
        WorldTransform:
            position = vec2(250.0, 560.0)
    GemB:
        WorldTransform:
            position = vec2(350.0, 410.0)
```

or external data import:

```cactus
place_table BlueGems from BlueGem = "levels/level1_gems.csv"
```

Those should be separate changes after `place` semantics are proven.

## Risks / Trade-offs

- **New top-level keyword:** Adds surface area. Mitigation: `place` is declarative and directly addresses game authoring scale.
- **Overlap with `unit`:** Mitigation: document `unit` as direct entity declaration and `place` as template instance declaration.
- **Future editor needs:** v1 placement names may not be enough for editor round-tripping. Mitigation: avoid overcommitting to stable runtime handles now.
- **Ordering semantics:** Must be explicit so generated setup remains predictable.

## Open Questions

- Should `pub place` ever exist, or should placed entities remain module-local content declarations?
- Should placement names become stable editor IDs later?
- Should empty placement bodies be accepted for “use template defaults exactly”?
- Should `place` be allowed inside future `level` blocks, or should top-level placement remain the only v1 form?
