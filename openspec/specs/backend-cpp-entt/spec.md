## Requirements
### Requirement: EnTT component struct generation
The backend SHALL generate EnTT-compatible component structs for each trait. Empty traits SHALL generate empty tag structs.

#### Scenario: Trait becomes component
- **WHEN** the decorated AST contains `trait Position:` with `var x: float` and `var y: float`
- **THEN** the backend generates `struct Position { float x; float y; };` usable as an EnTT component

#### Scenario: Empty trait becomes tag
- **WHEN** the decorated AST contains `trait Alive:` with no fields
- **THEN** the backend generates `struct Alive {};` as an EnTT tag component

### Requirement: Registry-based system generation
The backend SHALL generate system functions that use `entt::registry::view<Components...>()` for entity iteration. Filter clauses SHALL map to view template parameters. For each event handler the backend SHALL emit a second parameter `const EventType& <name>` where `<name>` is the handler alias if present, otherwise the event name. The backend SHALL NOT emit individual field parameters (e.g., `float dt`) — handler body code accesses fields via the event variable (e.g., `tick.dt`).

#### Scenario: System with filter
- **WHEN** a system has `filter:` listing `Position` and `Velocity`, and an `on tick:` handler
- **THEN** the backend generates a function `void SystemName_tick(entt::registry& registry, const TickEvent& tick)` using `registry.view<Position, Velocity>().each([](auto& pos, auto& vel) { ... })`

#### Scenario: on tick handler body accesses tick.dt
- **WHEN** a handler body references `tick.dt`
- **THEN** the generated C++ handler function receives `const TickEvent& tick` as a parameter and accesses `tick.dt` directly (no translation to bare `dt`)

#### Scenario: on tick with alias uses alias name in generated code
- **WHEN** `on tick as t:` is declared
- **THEN** the generated handler function receives `const TickEvent& t` as a parameter and body references use `t.dt`

#### Scenario: User event handler uses event name as variable
- **WHEN** `on PlayerDamaged:` handler body contains `h.health -= PlayerDamaged.amount`
- **THEN** the generated handler function receives `const PlayerDamagedEvent& PlayerDamaged` and the expression accesses `.amount`

#### Scenario: Marker lifecycle event handler receives empty-struct parameter
- **WHEN** a system has `on spawn:` handler (no fields)
- **THEN** the generated handler function signature is `void SystemName_spawn(entt::registry& registry, const SpawnEvent& spawn)` with an empty `SpawnEvent` struct

### Requirement: EnTT dispatcher event generation
The backend SHALL generate event structs and configure `entt::dispatcher` for event routing. `emit` statements SHALL map to `dispatcher.trigger<EventType>(...)`.

#### Scenario: Event emit
- **WHEN** a system handler contains `emit Damage(amount: 10)`
- **THEN** the backend generates `dispatcher.trigger<DamageEvent>(DamageEvent{10});`

#### Scenario: Event handler registration
- **WHEN** a trait has `on damage(amount: int):` handler
- **THEN** the backend generates dispatcher sink connection for the DamageEvent type

### Requirement: Entity creation from units
The backend SHALL generate entity creation code from `unit` declarations. Each `apply:` trait SHALL be added as a component to the created entity. `config:` values SHALL be set on the components.

#### Scenario: Unit with apply and config
- **WHEN** the decorated AST contains `unit Cactus:` with `apply:` listing `Position` and `Renderable` and `config:` setting position values
- **THEN** the backend generates `auto entity = registry.create();` followed by `registry.emplace<Position>(entity, ...);` for each trait

### Requirement: Code generation for `add` statement
The backend SHALL generate code for `AddTraitStmt` nodes. For the EnTT backend, marker adds use `registry.emplace_or_replace<TraitName>(entity)`, data-bearing adds initialize fields, and `to` targets use the resolved target entity.

#### Scenario: add marker trait generates emplace_or_replace
- **WHEN** `add Frozen` is compiled and `Frozen` is a marker trait
- **THEN** the generated code is `registry.emplace_or_replace<Frozen>(entity)`

### Requirement: Code generation for `remove` statement
The backend SHALL generate code for `RemoveTraitStmt` nodes. For the EnTT backend, `remove TraitName` compiles to `registry.remove<TraitName>(entity)` and `remove TraitName from target_expr` uses the resolved target entity.

#### Scenario: remove trait generates registry.remove call
- **WHEN** `remove Frozen` is compiled
- **THEN** the generated code is `registry.remove<Frozen>(entity)`

### Requirement: Persist field serialization hooks
The backend SHALL generate serialization functions for fields marked with `persist`, iterating over the registry view to serialize/deserialize marked components.

#### Scenario: Persist field in EnTT context
- **WHEN** a trait has `persist var health: int`
- **THEN** the backend generates code that iterates `registry.view<Health>()` to save/load the health component

### Requirement: Sync field replication hooks
The backend SHALL generate network replication stubs for fields marked with `sync`, using registry views to collect and apply delta updates.

#### Scenario: Sync field in EnTT context
- **WHEN** a trait has `sync var position: vec3`
- **THEN** the backend generates code that iterates the Position view to collect/apply network deltas

### Requirement: Raylib integration in generated code
The backend SHALL generate a main game loop using Raylib API calls by default, with EnTT registry and dispatcher initialized before the loop.

#### Scenario: Generated main with EnTT and Raylib
- **WHEN** the full pipeline runs on a complete `.cactus` program with EnTT backend selected
- **THEN** the generated code includes Raylib and EnTT headers, creates a registry and dispatcher, and runs the standard game loop

### Requirement: Compilable C++20 output with EnTT
The backend SHALL produce valid C++20 code that compiles with EnTT v3.x headers and Raylib linked.

#### Scenario: Generated code compiles
- **WHEN** the backend generates code from the cactus shop mini example
- **THEN** the output compiles without errors using a C++20 compiler with EnTT and Raylib available

### Requirement: Emit `cactus_runtime.h` include when extern funcs are in scope
The cpp-entt backend SHALL emit `#include "cactus_runtime.h"` in the generated C++ output when any extern func is present — either declared in the program itself or in any imported module's `ImportedSymbols.funcs` map (where `is_extern = true`).

The include SHALL be placed in the standard include block near the top of the generated file, after the fixed system headers (`<cstdint>`, `<string>`, etc.) and before the generated type definitions.

#### Scenario: Runtime header emitted when module imports std.math
- **WHEN** a program imports `std.math` (which declares extern funcs) and is compiled with cpp-entt
- **THEN** the generated file contains `#include "cactus_runtime.h"` in its include section

#### Scenario: Runtime header not emitted for extern-free programs
- **WHEN** a program contains no extern funcs and imports no modules with extern funcs
- **THEN** the generated file does NOT contain `#include "cactus_runtime.h"`

### Requirement: No C++ body emitted for extern funcs
The cpp-entt backend SHALL NOT emit a C++ function definition for any `FuncNode` or `ResolvedFunc` entry where `is_extern = true`. Extern funcs are satisfied by `cactus_runtime.h` and require no generated body.

#### Scenario: Extern func produces no generated function body
- **WHEN** the program declares `pub extern func lerp(a, b, t: float) float`
- **THEN** the generated C++ does NOT contain a definition `float lerp(float a, float b, float t) { ... }`

### Requirement: Stdlib extern system codegen — known patterns
The EnTT backend SHALL have built-in optimized implementations for `extern system` declarations that match known stdlib trait filter patterns. When an `extern system`'s filter contains a recognized stdlib trait, the backend generates a complete, optimized system body rather than a user-callback scaffold.

The recognized stdlib patterns and their generated implementations:

| Extern system filter includes | Generated implementation |
|---|---|
| `std.render.sprites.Renderer` + `std.transform.flat.WorldTransform` | Batched 2D sprite renderer using world-space transform data |
| `std.render.sprites.AnimatedSprite` | Sprite animation frame advancer (increments `frame` based on `fps` and dt) |
| `std.render.meshes.Renderer` + `std.transform.volume.WorldTransform` | 3D mesh render submission |
| `std.render.meshes.PointLight` + `std.transform.volume.WorldTransform` | Point light registration |
| `std.render.meshes.DirectionalLight` | Directional light registration |
| hierarchy propagation extern systems over `Parent` + `LocalTransform` + `WorldTransform` | Hierarchy-aware transform propagation |
| hierarchy cascade-delete extern systems over `Parent` | Recursive descendant deletion |

The backend recognizes these patterns by the **fully qualified trait names** (module path + trait name) from the filter clause.

#### Scenario: Stdlib SpriteRenderer generates batched render
- **WHEN** `extern system SpriteRenderer: filter: Position as pos, Renderer as r` is compiled (where both are stdlib traits)
- **THEN** the backend generates an optimized batched sprite rendering loop, not a per-entity callback

#### Scenario: hierarchy propagation recognized as known pattern
- **WHEN** the stdlib propagation extern system references `Parent`, `std.transform.flat.LocalTransform`, and `std.transform.flat.WorldTransform`
- **THEN** the backend generates built-in hierarchy propagation code rather than a user callback scaffold

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

### Requirement: Code generation for `order by:` clause
When a `SystemNode` has a non-empty `order_by` field, the EnTT backend SHALL emit a `registry.sort<T>(comparator)` call before the view iteration loop for each handler. `T` is the component type corresponding to the first sort key's alias. The comparator lambda implements the full multi-key lexicographic comparison.

Generated pattern for `order by: s.layer asc, p.pos.y desc`:

```cpp
registry.sort<Sprite>([&](const entt::entity a, const entt::entity b) {
    const auto& sa = registry.get<Sprite>(a);
    const auto& sb = registry.get<Sprite>(b);
    if (sa.layer != sb.layer)
        return sa.layer < sb.layer;
    const auto& pa = registry.get<Position>(a);
    const auto& pb = registry.get<Position>(b);
    return pa.pos.y > pb.pos.y;
});
```

The comparator is placed immediately before the `auto view = registry.view<...>()` call for the handler.

#### Scenario: Single sort key generates single-component comparator
- **WHEN** `order by: s.layer asc` is compiled
- **THEN** the generated comparator compares only `Sprite.layer` with `<`

#### Scenario: Multi-key sort generates if-chain comparator
- **WHEN** `order by: s.layer asc, p.pos.y desc` is compiled
- **THEN** the generated comparator uses `if (sa.layer != sb.layer) return sa.layer < sb.layer; return pa.pos.y > pb.pos.y;`

#### Scenario: desc direction reverses comparison
- **WHEN** a sort key has `desc` direction
- **THEN** the comparator uses `>` for that key's comparison

#### Scenario: asc direction uses less-than comparison
- **WHEN** a sort key has `asc` direction (explicit or default)
- **THEN** the comparator uses `<` for that key's comparison

#### Scenario: No order by generates no sort call
- **WHEN** a system has no `order by:` clause
- **THEN** no `registry.sort()` call is generated; the view iterates in default order

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

### Requirement: `self` compiles to the current EnTT entity handle
The EnTT backend SHALL compile `self` to the `entt::entity` currently being processed by the generated handler or iteration loop.

#### Scenario: `self` used as destroy target in EnTT
- **WHEN** `destroy self` is compiled
- **THEN** the generated code destroys the current `entity` rather than requiring a validity guard for another handle

#### Scenario: `self` stored into Parent.parent
- **WHEN** `Parent.parent = self` is compiled in a handler
- **THEN** the generated code writes the current `entity` handle into the `parent` field

### Requirement: EnTT backend recognizes hierarchy extern systems
The EnTT backend SHALL recognize stdlib hierarchy extern systems for transform propagation and cascade deletion and generate complete registry-aware implementations for them.

#### Scenario: hierarchy propagation extern system generates registry traversal
- **WHEN** the stdlib hierarchy propagation extern system is compiled
- **THEN** the backend emits registry-aware code that reads parent relationships and writes `WorldTransform` values

#### Scenario: hierarchy cascade extern system generates descendant destruction
- **WHEN** the stdlib hierarchy cascade-delete extern system is compiled
- **THEN** the backend emits registry-aware code that recursively destroys descendants of the removed entity

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

