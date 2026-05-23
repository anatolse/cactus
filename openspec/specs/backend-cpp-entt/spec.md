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
The backend SHALL generate system functions that iterate over the EnTT registry and apply declared filter clauses. For ordinary generated systems with non-empty filters, the backend SHALL use `entt::registry::view<Components...>()`-style iteration so filtering is performed by EnTT rather than by scanning every entity with early-exit guards. When `exclude:` clauses are present, the backend SHALL use native EnTT exclusion where possible. For each event handler the backend SHALL emit a second parameter `const EventType& <name>` where `<name>` is the handler alias if present, otherwise the event name. The backend SHALL NOT emit individual field parameters (e.g., `float dt`) — handler body code accesses fields via the event variable (e.g., `tick.dt`).

Projected traits SHALL participate in this same registry-based filtering by being materialized as registry components during the current frame.

#### Scenario: System with filter
- **WHEN** a system has `filter:` listing `Position` and `Velocity`, and an `on tick:` handler
- **THEN** the backend generates a function `void SystemName_tick(entt::registry& registry, const TickEvent& tick)` that iterates matching entities through an EnTT view and binds `Position` and `Velocity` component references for the handler body

#### Scenario: Non-matching entity does not terminate handler
- **WHEN** multiple live entities exist and the first live entity does not satisfy a system filter
- **THEN** generated system filtering continues considering later entities rather than returning from the entire handler

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

### Requirement: cpp-entt codegen consumes flattened composed archetypes
The cpp-entt backend SHALL generate units and spawned template instances from the semantic analyzer's flattened archetype representation after archetype-body template uses have been resolved and merged.

#### Scenario: Composed unit emits used-template components
- **WHEN** a unit uses `WalkerEnemy` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the generated cpp-entt setup code emplaces components from both templates exactly once according to the flattened archetype

#### Scenario: Spawned composed template emits used-template components
- **WHEN** a handler spawns `WalkerEnemy` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the generated spawn code emplaces all flattened component traits from the composed template

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
The backend SHALL support a standard runtime-driven game loop for EnTT projects, with EnTT registry and dispatcher initialized before frame execution. The default compiled-project integration SHALL be realized through generated project output that embeds a backend-generated `main()` and links against the standard Cactus EnTT backend/runtime library.

#### Scenario: Linked EnTT runtime integration
- **WHEN** the full pipeline runs on a complete `.cactus` program with the EnTT backend selected
- **THEN** the generated project-specific output contains the backend-generated executable entrypoint and links against the standard Cactus EnTT backend/runtime library, which provides reusable runtime integration for the project

### Requirement: EnTT standard executable output includes backend-generated entrypoint
The cpp-entt backend SHALL support standard executable builds by emitting a backend-generated `main()` into generated project output and linking that generated output with the standard Cactus EnTT backend/runtime library. The emitted `main()` SHALL be owned by the cpp-entt backend template and SHALL NOT be authored by CMake or a separate host source file.

#### Scenario: Generated EnTT executable contains backend-generated main
- **WHEN** a supported `.cactus` program is generated with the `cpp-entt` backend for the standard executable path
- **THEN** the generated C++ output contains a standalone `int main()` implementation emitted by the cpp-entt backend

#### Scenario: Generated EnTT executable links with runtime library
- **WHEN** the generated cpp-entt output is built as an executable
- **THEN** the executable links the generated C++ file containing `main()` with the standard Cactus EnTT backend/runtime library

#### Scenario: EnTT generated main initializes registry and dispatcher
- **WHEN** the cpp-entt backend-generated `main()` starts a generated project
- **THEN** it owns creation of `entt::registry` and `entt::dispatcher` and passes them to the generated setup, init, update, and render hooks

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

### Requirement: Emit `cactus_runtime.hpp` include when extern funcs are in scope
The cpp-entt backend SHALL emit `#include "cactus_runtime.hpp"` in the generated C++ output when any extern func is present — either declared in the program itself or in any imported module's `ImportedSymbols.funcs` map (where `is_extern = true`).

The include SHALL be placed in the standard include block near the top of the generated file, after the fixed system headers (`<cstdint>`, `<string>`, etc.) and before the generated type definitions.

#### Scenario: Runtime header emitted when module imports std.math
- **WHEN** a program imports `std.math` (which declares extern funcs) and is compiled with cpp-entt
- **THEN** the generated file contains `#include "cactus_runtime.hpp"` in its include section

#### Scenario: Runtime header not emitted for extern-free programs
- **WHEN** a program contains no extern funcs and imports no modules with extern funcs
- **THEN** the generated file does NOT contain `#include "cactus_runtime.hpp"`

### Requirement: No C++ body emitted for extern funcs
The cpp-entt backend SHALL NOT emit a C++ function definition for any `FuncNode` or `ResolvedFunc` entry where `is_extern = true`. Extern funcs are satisfied by `cactus_runtime.hpp` and require no generated body.

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

#### Scenario: Stdlib MeshRenderer binds to backend library implementation
- **WHEN** a program importing `std.render.meshes` is compiled with `cpp-entt` and the recognized `MeshRenderer` extern system filter matches fully qualified `std.transform.volume.WorldTransform` plus `std.render.meshes.Renderer`
- **THEN** the generated EnTT project output binds to the standard cpp-entt mesh-submission implementation rather than generic user-extern callback scaffolding
- **AND** it does not declare or require a user-provided `MeshRenderer_update(...)` implementation

#### Scenario: hierarchy propagation binds to backend library implementation
- **WHEN** the stdlib propagation extern system references `Parent`, `std.transform.flat.LocalTransform`, and `std.transform.flat.WorldTransform`
- **THEN** the generated EnTT project output binds to the standard Cactus EnTT backend/runtime library implementation for hierarchy propagation

#### Scenario: Non-stdlib traits do not use backend-library stdlib binding
- **WHEN** `extern system MySystem: filter: Position as pos, MyCustomTrait as c` is compiled and `MyCustomTrait` is user-defined
- **THEN** the backend does not treat the system as a stdlib backend-library implementation

### Requirement: EnTT backend runtime adapters realize sprite and mesh rendering through Raylib-backed frame passes
The cpp-entt backend SHALL upgrade recognized stdlib sprite and mesh render-system bindings from submission-only/debug-accounting behavior to real Raylib-backed runtime rendering behavior. Recognized `SpriteRenderer` and `MeshRenderer` output SHALL flow through backend-owned render-frame passes rather than stopping at asset validation and debug counters.

#### Scenario: Sprite renderer participates in backend-owned 2D Raylib rendering
- **WHEN** a program importing `std.render.sprites` is compiled with `cpp-entt` and the recognized `SpriteRenderer` extern system runs for a visible entity with a resolvable texture asset
- **THEN** the generated/runtime path queues or submits sprite draw work into the standard cpp-entt Raylib-backed 2D render pass

#### Scenario: Mesh renderer participates in backend-owned 3D Raylib rendering
- **WHEN** a program importing `std.render.meshes` is compiled with `cpp-entt` and the recognized `MeshRenderer` extern system runs for a visible entity with resolvable mesh/material assets
- **THEN** the generated/runtime path queues or submits mesh draw work into the standard cpp-entt Raylib-backed 3D render pass

### Requirement: EnTT render runtime provides deterministic default cameras for backend-owned render passes
The cpp-entt backend SHALL provide deterministic default camera behavior for its backend-owned 2D and 3D render passes so simple renderable programs remain viewable without authored camera setup. Active `std.camera.*` trait consumption is out of scope for this change.

#### Scenario: 2D render path falls back to default camera
- **WHEN** sprite rendering occurs in a cpp-entt program
- **THEN** the backend still renders using its default 2D camera configuration

#### Scenario: 3D render path falls back to default camera
- **WHEN** mesh rendering occurs in a cpp-entt program
- **THEN** the backend still renders using its default 3D camera configuration

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

#### Scenario: MeshRenderer included from module import
- **WHEN** a program uses `use std.render.meshes` and applies `std.render.meshes.Renderer` together with `std.transform.volume.WorldTransform`
- **THEN** the backend includes the recognized `MeshRenderer` system in the generated system schedule automatically without requiring the author to redeclare it locally

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

### Requirement: EnTT backend supports std.physics collider traits
The cpp-entt backend SHALL support the `std.physics.flat` and `std.physics.volume` collider traits used by authored programs and curated examples. This support SHALL be limited to the cpp-entt backend/runtime path and SHALL NOT require other backends to implement physics simulation.

#### Scenario: Imported 2D collider traits generate usable EnTT components
- **WHEN** a program imports `std.physics.flat` and applies `Collider` plus a supported shape trait to a unit
- **THEN** cpp-entt generation produces usable EnTT components for shared filtering data and the selected 2D shape dimensions initialized from authored values or stdlib defaults

#### Scenario: Imported 3D collider traits generate usable EnTT components
- **WHEN** a program imports `std.physics.volume` and applies `Collider` plus a supported shape trait to a unit
- **THEN** cpp-entt generation produces usable EnTT components for shared filtering data and the selected 3D shape dimensions initialized from authored values or stdlib defaults

#### Scenario: Collider recognition uses fully qualified stdlib trait identity
- **WHEN** a collider trait is imported through an alias such as `use std.physics.flat as phys` or `use std.physics.volume as phys` and used as `phys.Collider`, `phys.BoxCollider`, `phys.CircleCollider`, `phys.SphereCollider`, or `phys.CapsuleCollider`
- **THEN** the cpp-entt backend recognizes it as a stdlib collider trait rather than as an unrelated user-defined component

#### Scenario: EnTT runtime detects 2D primitive overlap for stdlib colliders
- **WHEN** two valid entities have `std.transform.flat.WorldTransform`, compatible `std.physics.flat.Collider` components, and supported 2D shape collider components whose primitive bounds overlap
- **THEN** the cpp-entt runtime detects the overlap using transform position plus shape-specific dimensions

#### Scenario: EnTT runtime detects 3D primitive overlap for stdlib colliders
- **WHEN** two valid entities have `std.transform.volume.WorldTransform`, compatible `std.physics.volume.Collider` components, and supported 3D shape collider components whose primitive bounds overlap
- **THEN** the cpp-entt runtime detects the overlap using transform position plus shape-specific dimensions

#### Scenario: EnTT runtime honors collider layer masks
- **WHEN** two colliders overlap geometrically but their layer and mask bitmasks do not allow interaction
- **THEN** the cpp-entt runtime does not report a collision between them

#### Scenario: EnTT runtime emits CollisionEnter for supported overlaps
- **WHEN** two compatible stdlib colliders begin overlapping in a cpp-entt program
- **THEN** the cpp-entt runtime emits or dispatches the matching stdlib `CollisionEnter` event payload for each participating entity

#### Scenario: Platformer compiles with stdlib collider usage
- **WHEN** `examples/platformer/platformer.cactus` uses `std.physics.flat.Collider` and a supported 2D shape collider and is generated for cpp-entt
- **THEN** the generated output compiles and links with the standard cpp-entt backend/runtime library without requiring user-provided collider callbacks

### Requirement: EnTT mesh render pass applies registered point lights to mesh shading
The cpp-entt backend SHALL treat recognized stdlib point-light registration as render-pass input for backend-owned mesh shading rather than debug-only accounting. During a render frame, enabled point lights registered through the recognized `std.render.meshes.PointLightSystem` binding SHALL contribute lighting data consumed by the backend-owned mesh pass.

#### Scenario: Enabled point light participates in the mesh pass
- **WHEN** a cpp-entt program registers an enabled `std.render.meshes.PointLight` and also submits a visible mesh in the same render frame
- **THEN** the backend-owned mesh pass retains that light as active lighting input for the frame

#### Scenario: Disabled point light does not contribute shading input
- **WHEN** a registered `std.render.meshes.PointLight` has `enabled = false`
- **THEN** the backend does not add that light to the active lighting inputs used for mesh shading in that frame

### Requirement: EnTT runtime supports at least two simultaneous point lights for stdlib mesh rendering
The cpp-entt backend-owned mesh render path SHALL support at least two enabled point lights contributing in the same frame so curated multi-light mesh examples render through the standard runtime path.

#### Scenario: Two active point lights are retained in one frame
- **WHEN** two enabled recognized stdlib point lights are registered before the mesh pass flushes
- **THEN** the cpp-entt runtime retains both lights for the frame instead of dropping to a single-light debug path

### Requirement: cpp-entt backend lowers bounded foreach over list values
The cpp-entt backend SHALL compile bounded foreach statements into C++ iteration over the evaluated list snapshot. The iterable expression SHALL be emitted once before the generated loop.

#### Scenario: Foreach iterable emitted once
- **WHEN** a handler contains `for hit in phys.query_overlap_all(self, mask, self):`
- **THEN** generated code evaluates `phys.query_overlap_all(...)` once into a temporary list value before iterating

#### Scenario: Foreach body emitted inside generated loop
- **WHEN** a foreach body emits `Damage` to `hit.entity`
- **THEN** generated code emits the body statements inside the C++ loop with `hit` bound to the current element

### Requirement: cpp-entt backend stores projected traits as transient registry components
The cpp-entt backend SHALL materialize projected traits as normal EnTT registry components during the current frame. The backend SHALL track projected component writes so that frame cleanup can remove components that were added only by projection and restore durable components that existed before being temporarily projected over. Newly generated helper identifiers for this tracking SHALL NOT use the `cactus` prefix.

#### Scenario: Project writes registry component
- **WHEN** generated code executes `project DamageFlash to target`
- **THEN** the backend writes or patches the `DamageFlash` component in the registry for `target` for the duration of the frame

#### Scenario: Project to stale target is no-op
- **WHEN** generated code executes `project DamageFlash to target` and `target` is stale/non-live
- **THEN** the operation is a safe no-op consistent with total `entity_id` semantics

#### Scenario: Projected-trait helpers avoid cactus prefix
- **WHEN** the backend emits helper storage or functions for registry-backed projected traits
- **THEN** the generated helper identifiers do not use the `cactus` prefix

#### Scenario: Project over durable component restores durable value
- **WHEN** an entity has durable `DamageFlash` before the frame and generated code projects `DamageFlash` to that entity during the frame
- **THEN** systems during the frame observe the projected value
- **AND** frame cleanup restores the pre-existing durable `DamageFlash` value

#### Scenario: Project-only component is removed at cleanup
- **WHEN** an entity did not have durable `DamageFlash` before the frame and generated code projects `DamageFlash` to that entity
- **THEN** systems during the frame observe `DamageFlash`
- **AND** frame cleanup removes `DamageFlash` from that entity

### Requirement: cpp-entt backend coalesces projected registry components
The cpp-entt backend SHALL maintain at most one projected value per `(entity, trait)` during a frame. Repeated projections to the same key SHALL patch or replace the current registry component while preserving the original pre-frame durable snapshot for cleanup.

#### Scenario: Repeated projection coalesces
- **WHEN** `DamageFlash` is projected twice to the same entity in one frame
- **THEN** later filter matching observes one `DamageFlash` value for that entity
- **AND** cleanup restores the value that existed before the first projection, if any

### Requirement: cpp-entt backend lowers `std.text.format` to C++20 `std::format`
The cpp-entt backend SHALL compile recognized `std.text.format` calls to C++20 `std::format` expressions. The generated C++ SHALL preserve the source format string and pass each Cactus formatting value in argument order after normal expression lowering.

Generated cpp-entt translation units that contain at least one lowered `std.text.format` call SHALL include `<format>`.

#### Scenario: Unqualified format call lowers to std::format
- **WHEN** authored code imports `std.text` and calls `format("HP: {}", hp)`
- **THEN** the backend emits a C++ expression equivalent to `std::format("HP: {}", hp)` and includes `<format>`

#### Scenario: Aliased format call lowers to std::format
- **WHEN** authored code imports `std.text as text` and calls `text.format("Score: {}", score)`
- **THEN** the backend emits a C++ expression equivalent to `std::format("Score: {}", score)` and includes `<format>`

#### Scenario: Non-format stdlib calls are not affected
- **WHEN** authored code calls `std.math.sqrt(x)`
- **THEN** the backend lowers it using existing stdlib math behavior and does not treat it as a text-format call

### Requirement: cpp-entt backend preserves C++20 format semantics
The cpp-entt backend SHALL preserve C++20 replacement-field syntax in generated format strings, including automatic placeholders, manual positional placeholders, format-spec tails, and escaped braces.

#### Scenario: Format specifier preserved
- **WHEN** authored code calls `text.format("time={:.2f}", seconds)`
- **THEN** the generated C++ format string remains `"time={:.2f}"`

#### Scenario: Manual placeholder order preserved
- **WHEN** authored code calls `text.format("{1} / {0}", first, second)`
- **THEN** the generated C++ format string remains `"{1} / {0}"`

### Requirement: cpp-entt system filters include projected registry components
For generated system handlers, the cpp-entt backend SHALL match entities that satisfy filter traits through registry components, including components materialized by projection earlier in the frame. Exclude traits SHALL also consider projected registry components.

#### Scenario: Filter matches projected trait through registry view
- **WHEN** an entity has durable `Health` and projected `DamageFlash`
- **AND** a later system filters `Health as hp` and `DamageFlash as flash`
- **THEN** the generated handler processes that entity through normal registry-based filtering and binds `flash` to the current projected component value

#### Scenario: Exclude skips projected trait through registry exclusion
- **WHEN** an entity has projected `Suppressed`
- **AND** a later system excludes `Suppressed`
- **THEN** the generated handler skips that entity for the current frame

### Requirement: cpp-entt backend clears projected registry components at frame boundary
The cpp-entt backend SHALL clear projected-trait state at the deterministic frame boundary after render processing completes by removing projected-only registry components and restoring any pre-existing durable component values that were temporarily replaced.

#### Scenario: Projected component not visible next frame
- **WHEN** an entity has projected `Highlighted` during one frame and did not have durable `Highlighted` before projection
- **THEN** the next frame does not match `Highlighted` unless it is projected again

#### Scenario: Durable component survives projection cleanup
- **WHEN** an entity had durable `Highlighted` before projection and receives a projected `Highlighted` value during the frame
- **THEN** the next frame observes the original durable `Highlighted` value unless authored code changed or removed it durably

