## Purpose
Define the required behavior of the EnTT-based C++ backend, including registry-oriented code generation, event integration, and correctness expectations for generated output.
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
The backend SHALL support a standard runtime-driven game loop for EnTT projects, with EnTT registry and dispatcher initialized before frame execution. The default compiled-project integration SHALL be realized through generated project glue linked against the standard Cactus EnTT backend/runtime library rather than requiring the generated output to embed a complete standalone `main()` implementation.

#### Scenario: Linked EnTT runtime integration
- **WHEN** the full pipeline runs on a complete `.cactus` program with the EnTT backend selected
- **THEN** the generated project-specific output links against the standard Cactus EnTT backend/runtime library, which provides the reusable runtime/game-loop integration for the project

### Requirement: Compilable C++20 output with EnTT
The backend SHALL produce valid C++20 generated project glue that compiles and links successfully with EnTT v3.x headers, Raylib, and the standard Cactus EnTT backend/runtime library.

#### Scenario: Generated EnTT project links successfully
- **WHEN** the backend generates C++ from a supported example or authored project
- **THEN** the generated project-specific C++ compiles without errors and links successfully when combined with the standard Cactus EnTT backend/runtime library and required dependencies

#### Scenario: Curated example compilation coverage validates linked EnTT projects
- **WHEN** automated example-compilation integration coverage runs for an example configured for the EnTT backend path
- **THEN** the generated C++ project glue compiles successfully and links against the project's configured EnTT-enabled toolchain and the standard Cactus EnTT backend/runtime library

### Requirement: EnTT backend provides complete stdlib extern function coverage
The cpp-entt backend and its linked runtime/library SHALL provide concrete implementations or binding paths for stdlib-declared extern functions that are part of the supported stdlib surface and are not blocked by missing language/runtime features, including scalar math, vec2/vec3 helpers, quaternion helpers, and input query functions.

Pure helpers that do not depend on runtime state SHALL use allocation-free implementations and SHALL be declared `constexpr` and `noexcept` where permitted by the underlying C++ operations.

#### Scenario: Math extern declarations resolve through runtime library
- **WHEN** a program imports stdlib modules that declare supported math/vector/quaternion extern functions and is compiled with cpp-entt
- **THEN** the generated project links successfully and the extern calls resolve through concrete cpp-entt/shared runtime-library implementations

#### Scenario: Input extern declarations resolve through EnTT runtime adapter
- **WHEN** a program imports `std.input` and is compiled with cpp-entt
- **THEN** the generated project links successfully and the input extern calls resolve through concrete cpp-entt runtime adapter implementations

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
The EnTT backend SHALL treat recognized stdlib `extern system` declarations as backend-library-provided behavior. When an `extern system`'s filter contains a recognized stdlib trait pattern, generated project output SHALL bind to the corresponding implementation in the standard Cactus EnTT backend/runtime library rather than emitting a project-local inline implementation body.

The recognized stdlib patterns and their generated implementations remain:

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

#### Scenario: Stdlib SpriteRenderer binds to backend library implementation
- **WHEN** `extern system SpriteRenderer: filter: Position as pos, Renderer as r` is compiled and the filter matches a recognized stdlib pattern
- **THEN** the generated EnTT project output binds to the standard Cactus EnTT backend/runtime library implementation for sprite rendering rather than emitting a project-local renderer body

#### Scenario: hierarchy propagation binds to backend library implementation
- **WHEN** the stdlib propagation extern system references `Parent`, `std.transform.flat.LocalTransform`, and `std.transform.flat.WorldTransform`
- **THEN** the generated EnTT project output binds to the standard Cactus EnTT backend/runtime library implementation for hierarchy propagation

#### Scenario: Non-stdlib traits do not use backend-library stdlib binding
- **WHEN** `extern system MySystem: filter: Position as pos, MyCustomTrait as c` is compiled and `MyCustomTrait` is user-defined
- **THEN** the backend does not treat the system as a stdlib backend-library implementation

### Requirement: User-defined extern system codegen — C++ scaffold
For `extern system` declarations with user-defined (non-stdlib) traits, the EnTT backend SHALL generate typed declarations and scheduling glue that call user-provided callback implementations from the project’s user library. The backend SHALL generate the iteration infrastructure (including `order by:` support when present), and the final linked project SHALL obtain the callback implementation from the user library rather than from a generated implementation body.

The callback name convention remains `<SystemName>_update`. The signature includes typed references to all filter components.

#### Scenario: User extern system generates library-facing declaration
- **WHEN** `extern system MyParticleSystem: filter: Position as pos, ParticleEmitter as pe` is compiled
- **THEN** the generated EnTT project output declares a typed `MyParticleSystem_update(...)` callback contract and emits scheduling glue that expects the implementation to be supplied by the user library

#### Scenario: order by in user extern system still generates sort call
- **WHEN** a user `extern system` has `order by: pos.pos.y asc`
- **THEN** the generated EnTT scheduling glue includes a `registry.sort<Position>(...)` call before invoking the user-library callback for each matched entity

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

### Requirement: EnTT backend recognizes the full supported stdlib extern system set
The cpp-entt backend SHALL recognize supported stdlib extern system patterns and bind them to backend-library implementations rather than treating them as generic user extern systems.

The supported recognized set SHALL include, at minimum, the stdlib extern system patterns whose required language/runtime features are already implemented, such as hierarchy propagation, cascade deletion, sprite rendering, animated sprite advancement, mesh rendering, point-light registration, and directional-light registration where applicable.

#### Scenario: Supported stdlib renderer binds to backend library
- **WHEN** a recognized stdlib render extern system is compiled with cpp-entt
- **THEN** generated output binds to the corresponding cpp-entt backend-library implementation rather than user-library callback scaffolding

#### Scenario: Supported hierarchy behavior binds to backend library
- **WHEN** a recognized stdlib hierarchy extern system is compiled with cpp-entt
- **THEN** generated output binds to the corresponding cpp-entt backend-library implementation rather than project-local traversal logic

### Requirement: EnTT backend runtime-owned dynamic storage uses pmr containers
When the cpp-entt backend/runtime requires dynamic storage for performance-critical generated runtime behavior, it SHALL use `std::pmr` containers/resources rather than default-allocator standard containers, unless the implementation is allocation-free.

#### Scenario: Temporary backend-owned collections use pmr
- **WHEN** cpp-entt runtime/library code creates dynamic scratch collections for stdlib-owned backend behavior
- **THEN** those collections use `std::pmr` storage or an allocation-free equivalent

### Requirement: EnTT backend stdlib coverage is tested behaviorally
The cpp-entt backend SHALL include tests that verify stdlib extern function correctness, recognized extern system binding, and representative runtime behavior for supported stdlib backend features.

#### Scenario: Extern function behavior is tested
- **WHEN** the cpp-entt backend test suite runs
- **THEN** it includes behavioral tests for supported stdlib math/vector/quaternion/input extern functions

#### Scenario: Recognized extern system behavior is tested
- **WHEN** the cpp-entt backend test suite runs
- **THEN** it includes tests covering recognized hierarchy and render extern system behavior or binding outcomes

