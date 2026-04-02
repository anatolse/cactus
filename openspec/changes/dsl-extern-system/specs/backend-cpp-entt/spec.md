## ADDED Requirements

### Requirement: Stdlib extern system codegen — known patterns
The EnTT backend SHALL have built-in optimized implementations for `extern system` declarations that match known stdlib trait filter patterns. When an `extern system`'s filter contains a recognized stdlib trait, the backend generates a complete, optimized system body rather than a user-callback scaffold.

The recognized stdlib patterns and their generated implementations:

| Extern system filter includes | Generated implementation |
|---|---|
| `std.render.sprites.Renderer` + `std.transform.flat.Position` | Batched 2D sprite renderer (sorted by layer, then Y) |
| `std.render.sprites.AnimatedSprite` | Sprite animation frame advancer (increments `frame` based on `fps` and dt) |
| `std.render.meshes.Renderer` + `std.transform.volume.Transform` | 3D mesh render submission |
| `std.render.meshes.PointLight` + `std.transform.volume.Transform` | Point light registration |
| `std.render.meshes.DirectionalLight` | Directional light registration |

The backend recognizes these patterns by the **fully qualified trait names** (module path + trait name) from the filter clause.

#### Scenario: Stdlib SpriteRenderer generates batched render
- **WHEN** `extern system SpriteRenderer: filter: Position as pos, Renderer as r` is compiled (where both are stdlib traits)
- **THEN** the backend generates an optimized batched sprite rendering loop, not a per-entity callback

#### Scenario: Non-stdlib traits do not trigger known-pattern generation
- **WHEN** `extern system MySystem: filter: Position as pos, MyCustomTrait as c` is compiled (where `MyCustomTrait` is user-defined)
- **THEN** the backend generates a C++ scaffold, not a known-pattern implementation

### Requirement: User-defined extern system codegen — C++ scaffold
For `extern system` declarations with user-defined (non-stdlib) traits, the EnTT backend SHALL generate:
1. A C++ header file declaring the user callback with typed component references
2. The iteration infrastructure (view creation, sort if `order by:` present, iteration loop)
3. A call to the user callback for each matched entity (or batch form at backend's discretion)

The callback name convention is `<SystemName>_update`. The signature includes typed references to all filter components.

```cpp
// Generated header (game_generated.h):
void MyParticleSystem_update(entt::registry& registry,
                              entt::entity entity,
                              Position& pos,
                              ParticleEmitter& pe);

// Generated implementation scaffold (game_generated.cpp):
void MyParticleSystem_tick(entt::registry& registry) {
    auto view = registry.view<Position, ParticleEmitter>();
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& pe = view.get<ParticleEmitter>(entity);
        MyParticleSystem_update(registry, entity, pos, pe);
    }
}
```

#### Scenario: User extern system generates C++ scaffold
- **WHEN** `extern system MyParticleSystem: filter: Position as pos, ParticleEmitter as pe` is compiled
- **THEN** a typed `MyParticleSystem_update(...)` callback declaration is generated in the output header

#### Scenario: order by in user extern system generates sort call
- **WHEN** user `extern system` has `order by: pos.pos.y asc`
- **THEN** the generated scaffold includes a `registry.sort<Position>(...)` call before the iteration loop

### Requirement: Stdlib extern systems are auto-included in the program output
When a program imports a stdlib module containing `extern system` declarations (e.g., `use std.render.sprites`), the backend SHALL include those extern systems in the generated output automatically. The author does not need to explicitly include them.

#### Scenario: SpriteRenderer included from module import
- **WHEN** a program uses `use std.render.sprites` and applies `Renderer` to any entity
- **THEN** the backend includes `SpriteRenderer` in the generated system schedule automatically
