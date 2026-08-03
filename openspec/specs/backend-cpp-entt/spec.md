## Purpose
Define the required behavior of the EnTT-based C++ backend, including registry-oriented code generation, event integration, and correctness expectations for generated output.
## Requirements
### Requirement: EnTT component struct generation
The backend SHALL generate EnTT-compatible component structs for each resolved trait. Empty traits SHALL generate empty tag structs. The generated C++ type name SHALL be derived from the trait's resolved symbol identity and SHALL be deterministic and collision-free for distinct trait symbols.

#### Scenario: Trait becomes component
- **WHEN** the resolved semantic program contains trait symbol `game.components.Position` with fields `var x: float` and `var y: float`
- **THEN** the backend generates a collision-free C++ component struct derived from `game.components.Position`, such as `game_components__Position`, with fields `float x; float y;`

#### Scenario: Empty trait becomes tag
- **WHEN** the resolved semantic program contains marker trait symbol `game.state.Alive`
- **THEN** the backend generates an empty tag component struct using the C++ name derived from `game.state.Alive`

#### Scenario: Same local trait name from different modules produces different C++ types
- **WHEN** traits `std.transform.flat.WorldTransform` and `std.transform.volume.WorldTransform` are both present
- **THEN** the backend generates two distinct C++ component type names

### Requirement: Code generation consumes resolved symbol identities
The cpp-entt backend SHALL consume resolved typed symbol identities for all module-scope declarations and references. It MUST NOT perform semantic name resolution by scanning `UseNode` declarations, resolving aliases, interpreting unique unqualified imports, or falling back from source-spelled strings to declaration maps.

#### Scenario: Alias-qualified trait lowered without alias lookup
- **WHEN** semantic analysis resolved source `phys.Body` to trait symbol `std.physics.flat.Body`
- **THEN** cpp-entt codegen emits the component name derived from `std.physics.flat.Body` without inspecting the `phys` alias

#### Scenario: Stdlib function binding keyed by resolved identity
- **WHEN** semantic analysis resolved a call to function symbol `std.input.mouse_delta`
- **THEN** cpp-entt lowers the call using the stdlib/runtime binding for `std.input.mouse_delta` rather than searching imported modules for a source spelling

#### Scenario: Query lowering keyed by resolved identity
- **WHEN** semantic analysis resolved a query call to `std.physics.volume.query.raycast`
- **THEN** cpp-entt selects the volume raycast lowering from that resolved function identity

#### Scenario: User declaration with stdlib-like local name does not bind stdlib behavior
- **WHEN** module `game.custom` declares `trait WorldTransform` and uses it in a rule
- **THEN** cpp-entt treats it as `game.custom.WorldTransform` and does not apply stdlib transform behavior unless the resolved symbol identity is the stdlib symbol

### Requirement: Registry-based system generation
The backend SHALL generate system functions that iterate over the EnTT registry and apply declared filter clauses. For ordinary generated systems with non-empty filters, the backend SHALL use `entt::registry::view<Components...>()`-style iteration so filtering is performed by EnTT rather than by scanning every entity with early-exit guards. When `exclude:` clauses are present, the backend SHALL use native EnTT exclusion where possible. For each event handler the backend SHALL emit a second parameter `const EventType& <name>` where `<name>` is the handler alias if present, otherwise the event name. The backend SHALL NOT emit individual field parameters (e.g., `float dt`) — handler body code accesses fields via the event variable (e.g., `tick.dt`).

Projected traits SHALL participate in this same registry-based filtering by being materialized as registry components during the current frame.

#### Scenario: System with filter
- **WHEN** a rule has `filter:` listing `Position` and `Velocity`, and an `on tick:` handler
- **THEN** the backend generates a function `void RuleName_tick(entt::registry& registry, const TickEvent& tick)` that iterates matching entities through an EnTT view and binds `Position` and `Velocity` component references for the handler body

#### Scenario: Non-matching entity does not terminate handler
- **WHEN** multiple live entities exist and the first live entity does not satisfy a rule filter
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
- **WHEN** a rule has `on spawn:` handler (no fields)
- **THEN** the generated handler function signature is `void RuleName_spawn(entt::registry& registry, const SpawnEvent& spawn)` with an empty `SpawnEvent` struct

### Requirement: EnTT dispatcher event generation
The backend SHALL generate event structs and configure `entt::dispatcher` for event routing. `emit` statements SHALL map to `dispatcher.trigger<EventType>(...)`.

#### Scenario: Event emit
- **WHEN** a rule handler contains `emit Damage(amount: 10)`
- **THEN** the backend generates `dispatcher.trigger<DamageEvent>(DamageEvent{10});`

#### Scenario: Event handler registration
- **WHEN** a trait has `on damage(amount: int):` handler
- **THEN** the backend generates dispatcher sink connection for the DamageEvent type

### Requirement: Entity creation from units
The backend SHALL generate entity creation code from `entity` declarations. Each nested trait entry SHALL be added as a component to the created entity with authored or default field values.

#### Scenario: Entity with nested trait config
- **WHEN** the decorated AST contains `entity Cactus:` with nested `Position:` and `Renderable:` trait entries
- **THEN** the backend generates `auto entity = registry.create();` followed by `registry.emplace<Position>(entity, ...);` and `registry.emplace<Renderable>(entity, ...);`

### Requirement: cpp-entt codegen consumes flattened composed archetypes
The cpp-entt backend SHALL generate inline entities, template-backed entities, and spawned template instances from the semantic analyzer's flattened archetype representation after archetype-body template uses have been resolved and merged.

#### Scenario: Composed entity emits used-template components
- **WHEN** an inline entity uses `WalkerEnemy` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the generated cpp-entt setup code emplaces components from both templates exactly once according to the flattened archetype

#### Scenario: Spawned composed template emits used-template components
- **WHEN** a handler spawns `WalkerEnemy` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the generated spawn code emplaces all flattened component traits from the composed template

### Requirement: cpp-entt setup instantiates template-backed entities

The cpp-entt backend SHALL instantiate each analyzed `entity Name from TemplateName:` declaration during generated module/world setup using the flattened template-backed entity archetype.

#### Scenario: Template-backed entity emits template components plus overrides
- **WHEN** `entity Gem1 from BlueGem:` overrides `WorldTransform.position`
- **THEN** generated setup code creates one entity with all components from `BlueGem` and the overridden `WorldTransform.position` value

### Requirement: Inline and template-backed entity creation order is deterministic

The cpp-entt backend SHALL preserve source declaration order among inline entities and template-backed entities within a module when generating setup code.

#### Scenario: Mixed entity order preserved
- **WHEN** a source module declares `entity A:`, then `entity B from T:`, then `entity C:`
- **THEN** generated setup code creates A before B and B before C

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

### Requirement: Stdlib extern rule codegen — known patterns
The EnTT backend SHALL treat recognized stdlib `extern rule` declarations as backend-library-provided behavior. When an `extern rule`'s filter contains a recognized stdlib trait pattern, generated project output SHALL bind to the corresponding implementation in the standard Cactus EnTT backend/runtime library rather than emitting a project-local inline implementation body.

The recognized stdlib patterns and their generated implementations remain:

| Extern rule filter includes | Generated implementation |
|---|---|
| `std.render.sprites.Renderer` + `std.transform.flat.WorldTransform` | Batched 2D sprite renderer using world-space transform data |
| `std.render.sprites.AnimatedSprite` | Sprite animation frame advancer (increments `frame` based on `fps` and dt) |
| `std.render.meshes.Renderer` + `std.transform.volume.WorldTransform` | 3D mesh render submission |
| `std.render.meshes.PointLight` + `std.transform.volume.WorldTransform` | Point light registration |
| `std.render.meshes.DirectionalLight` | Directional light registration |
| hierarchy propagation extern rules over `Parent` + `LocalTransform` + `WorldTransform` | Hierarchy-aware transform propagation |
| hierarchy cascade-delete extern rules over `Parent` | Recursive descendant deletion |

The backend recognizes these patterns by the **fully qualified trait names** (module path + trait name) from the filter clause.

#### Scenario: Stdlib SpriteRenderer binds to backend library implementation
- **WHEN** `extern rule SpriteRenderer: filter: Position as pos, Renderer as r` is compiled and the filter matches a recognized stdlib pattern
- **THEN** the generated EnTT project output binds to the standard Cactus EnTT backend/runtime library implementation for sprite rendering rather than emitting a project-local renderer body

#### Scenario: Stdlib MeshRenderer binds to backend library implementation
- **WHEN** a program importing `std.render.meshes` is compiled with `cpp-entt` and the recognized `MeshRenderer` extern rule filter matches fully qualified `std.transform.volume.WorldTransform` plus `std.render.meshes.Renderer`
- **THEN** the generated EnTT project output binds to the standard cpp-entt mesh-submission implementation rather than generic user-extern callback scaffolding
- **AND** it does not declare or require a user-provided `MeshRenderer_update(...)` implementation

#### Scenario: hierarchy propagation binds to backend library implementation
- **WHEN** the stdlib propagation extern rule references `Parent`, `std.transform.flat.LocalTransform`, and `std.transform.flat.WorldTransform`
- **THEN** the generated EnTT project output binds to the standard Cactus EnTT backend/runtime library implementation for hierarchy propagation

#### Scenario: Non-stdlib traits do not use backend-library stdlib binding
- **WHEN** `extern rule MyRule: filter: Position as pos, MyCustomTrait as c` is compiled and `MyCustomTrait` is user-defined
- **THEN** the backend does not treat the rule as a stdlib backend-library implementation

### Requirement: EnTT backend runtime adapters realize sprite and mesh rendering through Raylib-backed frame passes
The cpp-entt backend SHALL upgrade recognized stdlib sprite and mesh render-rule bindings from submission-only/debug-accounting behavior to real Raylib-backed runtime rendering behavior. Recognized `SpriteRenderer` and `MeshRenderer` output SHALL flow through backend-owned render-frame passes rather than stopping at asset validation and debug counters.

#### Scenario: Sprite renderer participates in backend-owned 2D Raylib rendering
- **WHEN** a program importing `std.render.sprites` is compiled with `cpp-entt` and the recognized `SpriteRenderer` extern rule runs for a visible entity with a resolvable texture asset
- **THEN** the generated/runtime path queues or submits sprite draw work into the standard cpp-entt Raylib-backed 2D render pass

#### Scenario: Mesh renderer participates in backend-owned 3D Raylib rendering
- **WHEN** a program importing `std.render.meshes` is compiled with `cpp-entt` and the recognized `MeshRenderer` extern rule runs for a visible entity with resolvable mesh/material assets
- **THEN** the generated/runtime path queues or submits mesh draw work into the standard cpp-entt Raylib-backed 3D render pass

### Requirement: EnTT render runtime provides deterministic default cameras for backend-owned render passes
The cpp-entt backend SHALL provide deterministic default camera behavior for its backend-owned 2D and 3D render passes so simple renderable programs remain viewable without authored camera setup. Active `std.camera.*` trait consumption is out of scope for this change.

#### Scenario: 2D render path falls back to default camera
- **WHEN** sprite rendering occurs in a cpp-entt program
- **THEN** the backend still renders using its default 2D camera configuration

#### Scenario: 3D render path falls back to default camera
- **WHEN** mesh rendering occurs in a cpp-entt program
- **THEN** the backend still renders using its default 3D camera configuration

### Requirement: User-defined extern rule codegen — C++ scaffold
For `extern rule` declarations with user-defined (non-stdlib) traits, the EnTT backend SHALL generate typed declarations and scheduling glue that call user-provided callback implementations from the project’s user library. The backend SHALL generate the iteration infrastructure (including `order by:` support when present), and the final linked project SHALL obtain the callback implementation from the user library rather than from a generated implementation body.

The callback name convention remains `<RuleName>_update`. The signature includes typed references to all filter components.

#### Scenario: User extern rule generates library-facing declaration
- **WHEN** `extern rule MyParticleRule: filter: Position as pos, ParticleEmitter as pe` is compiled
- **THEN** the generated EnTT project output declares a typed `MyParticleRule_update(...)` callback contract and emits scheduling glue that expects the implementation to be supplied by the user library

#### Scenario: order by in user extern rule still generates sort call
- **WHEN** a user `extern rule` has `order by: pos.pos.y asc`
- **THEN** the generated EnTT scheduling glue includes a `registry.sort<Position>(...)` call before invoking the user-library callback for each matched entity

### Requirement: Stdlib extern rules are auto-included in the program output
When a program imports a stdlib module containing `extern rule` declarations (e.g., `use std.render.sprites`), the backend SHALL include those extern rules in the generated output automatically. The author does not need to explicitly include them.

#### Scenario: SpriteRenderer included from module import
- **WHEN** a program uses `use std.render.sprites` and applies `Renderer` to any entity
- **THEN** the backend includes `SpriteRenderer` in the generated system schedule automatically

#### Scenario: MeshRenderer included from module import
- **WHEN** a program uses `use std.render.meshes` and applies `std.render.meshes.Renderer` together with `std.transform.volume.WorldTransform`
- **THEN** the backend includes the recognized `MeshRenderer` rule in the generated system schedule automatically without requiring the author to redeclare it locally

### Requirement: Code generation for `order by:` clause
When a `RuleNode` has a non-empty `order_by` field, the EnTT backend SHALL emit a `registry.sort<T>(comparator)` call before the view iteration loop for each handler. `T` is the component type corresponding to the first sort key's alias. The comparator lambda implements the full multi-key lexicographic comparison.

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
- **WHEN** a rule has no `order by:` clause
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

### Requirement: EnTT backend recognizes hierarchy extern rules
The EnTT backend SHALL recognize stdlib hierarchy extern rules for transform propagation and cascade deletion and generate complete registry-aware implementations for them.

#### Scenario: hierarchy propagation extern rule generates registry traversal
- **WHEN** the stdlib hierarchy propagation extern rule is compiled
- **THEN** the backend emits registry-aware code that reads parent relationships and writes `WorldTransform` values

#### Scenario: hierarchy cascade extern rule generates descendant destruction
- **WHEN** the stdlib hierarchy cascade-delete extern rule is compiled
- **THEN** the backend emits registry-aware code that recursively destroys descendants of the removed entity

### Requirement: cpp-entt lowers pair domains to deterministic snapshot products
The cpp-entt backend SHALL lower a pair handler to two typed EnTT membership passes, stable entity-creation-order snapshots, and left-binding-major nested iteration. It SHALL avoid materializing the complete Cartesian-product tuple list and SHALL bind selected component data as immutable access.

#### Scenario: Same trait selected by both bindings has distinct generated access
- **WHEN** both bindings require Collider
- **THEN** generated code retrieves separate const Collider access for each bound entity without identifier collision

#### Scenario: Empty binding snapshot produces no tuple execution
- **WHEN** either pair binding has no live matches
- **THEN** the generated handler body executes zero times

#### Scenario: Projected membership cannot change current product
- **WHEN** a tuple projects a required trait onto another entity
- **THEN** generated iteration continues over the snapshots created before the first tuple

### Requirement: cpp-entt maintains stable entity creation ordinals
The generated cpp-entt runtime SHALL assign every load-time and spawned entity a monotonic, non-reused creation ordinal and SHALL use that ordinal to sort relation snapshots. Hierarchical load/spawn creation SHALL retain deterministic parent-first order, and committed spawns SHALL receive ordinals in command-commit order.

#### Scenario: Repeated generation yields identical tuple event order
- **WHEN** the same deterministic program state executes a pair handler in two runs
- **THEN** emitted tuple occurrence order is identical

### Requirement: Targeted event dispatch guarded by validity check
When an event is emitted with a `to expr` clause, cpp-entt SHALL evaluate the target once, retain it in the queued event envelope, and check `registry.valid(target)` before delivery. A stale target SHALL drop the event. A live target SHALL constrain unary consumers to that entity and pair consumers to tuples incident to that entity; the backend MUST NOT implement targeted emission as a validity guard around broadcast dispatch.

```cpp
// Conceptual generated result for: emit PlayerDamaged to walker_id
generated_emit_targeted_event(PlayerDamagedEvent{...}, walker_id);
```

#### Scenario: targeted emit to stale entity is dropped
- **WHEN** `emit SomeEvent to dead_id` executes and `dead_id` is stale at delivery
- **THEN** the event is not delivered to any handler and no error occurs

#### Scenario: targeted emit does not broadcast across unary view
- **WHEN** a valid targeted event has a unary filtered consumer with several matching entities
- **THEN** generated dispatch invokes the consumer only for the target when it satisfies the filter

#### Scenario: target survives cascade deferral
- **WHEN** a targeted event occurrence is moved to the deferred event queue
- **THEN** its recipient is preserved for later dispatch

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

### Requirement: EnTT backend recognizes the full supported stdlib extern rule set
The cpp-entt backend SHALL recognize supported stdlib extern rule patterns and bind them to backend-library implementations rather than treating them as generic user extern rules.

The supported recognized set SHALL include, at minimum, the stdlib extern rule patterns whose required language/runtime features are already implemented, such as hierarchy propagation, cascade deletion, sprite rendering, animated sprite advancement, mesh rendering, point-light registration, and directional-light registration where applicable.

#### Scenario: Supported stdlib renderer binds to backend library
- **WHEN** a recognized stdlib render extern rule is compiled with cpp-entt
- **THEN** generated output binds to the corresponding cpp-entt backend-library implementation rather than user-library callback scaffolding

#### Scenario: Supported hierarchy behavior binds to backend library
- **WHEN** a recognized stdlib hierarchy extern rule is compiled with cpp-entt
- **THEN** generated output binds to the corresponding cpp-entt backend-library implementation rather than project-local traversal logic

### Requirement: EnTT backend runtime-owned dynamic storage uses pmr containers
When the cpp-entt backend/runtime requires dynamic storage for performance-critical generated runtime behavior, it SHALL use `std::pmr` containers/resources rather than default-allocator standard containers, unless the implementation is allocation-free.

#### Scenario: Temporary backend-owned collections use pmr
- **WHEN** cpp-entt runtime/library code creates dynamic scratch collections for stdlib-owned backend behavior
- **THEN** those collections use `std::pmr` storage or an allocation-free equivalent

### Requirement: EnTT backend stdlib coverage is tested behaviorally
The cpp-entt backend SHALL include tests that verify stdlib extern function correctness, recognized extern rule binding, and representative runtime behavior for supported stdlib backend features.

#### Scenario: Extern function behavior is tested
- **WHEN** the cpp-entt backend test suite runs
- **THEN** it includes behavioral tests for supported stdlib math/vector/quaternion/input extern functions

#### Scenario: Recognized extern rule behavior is tested
- **WHEN** the cpp-entt backend test suite runs
- **THEN** it includes tests covering recognized hierarchy and render extern rule behavior or binding outcomes

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
The cpp-entt backend SHALL treat recognized stdlib point-light registration as render-pass input for backend-owned mesh shading rather than debug-only accounting. During a render frame, enabled point lights registered through the recognized `std.render.meshes.PointLightRender` binding SHALL contribute lighting data consumed by the backend-owned mesh pass.

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
- **THEN** rules during the frame observe the projected value
- **AND** frame cleanup restores the pre-existing durable `DamageFlash` value

#### Scenario: Project-only component is removed at cleanup
- **WHEN** an entity did not have durable `DamageFlash` before the frame and generated code projects `DamageFlash` to that entity
- **THEN** rules during the frame observe `DamageFlash`
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

### Requirement: cpp-entt rule filters include projected registry components
For generated rule handlers, the cpp-entt backend SHALL match entities that satisfy filter traits through registry components, including components materialized by projection earlier in the frame. Exclude traits SHALL also consider projected registry components.

#### Scenario: Filter matches projected trait through registry view
- **WHEN** an entity has durable `Health` and projected `DamageFlash`
- **AND** a later rule filters `Health as hp` and `DamageFlash as flash`
- **THEN** the generated handler processes that entity through normal registry-based filtering and binds `flash` to the current projected component value

#### Scenario: Exclude skips projected trait through registry exclusion
- **WHEN** an entity has projected `Suppressed`
- **AND** a later rule excludes `Suppressed`
- **THEN** the generated handler skips that entity for the current frame

### Requirement: cpp-entt backend clears projected registry components at frame boundary
The cpp-entt backend SHALL clear projected-trait state at the deterministic frame boundary after render processing completes by removing projected-only registry components and restoring any pre-existing durable component values that were temporarily replaced.

#### Scenario: Projected component not visible next frame
- **WHEN** an entity has projected `Highlighted` during one frame and did not have durable `Highlighted` before projection
- **THEN** the next frame does not match `Highlighted` unless it is projected again

#### Scenario: Durable component survives projection cleanup
- **WHEN** an entity had durable `Highlighted` before projection and receives a projected `Highlighted` value during the frame
- **THEN** the next frame observes the original durable `Highlighted` value unless authored code changed or removed it durably

### Requirement: Backend recognizes TextRenderer2D and emits DrawTextPro-based 2D text rendering
The cpp-entt backend SHALL recognize the `extern rule TextRenderer2D` from `std.render.text` (identified by its filter containing `std.transform.flat.WorldTransform` and `TextLabel`) and emit a rule body that submits text draw calls for the backend-owned 2D render pass. The generated code SHALL use `DrawTextPro` (or equivalent rotation-capable raylib function) so that `WorldTransform.rotation` (radians) is applied to the rendered text.

#### Scenario: TextRenderer2D generates a draw-text submission
- **WHEN** the backend encounters a recognized `TextRenderer2D` extern rule
- **THEN** it emits a tick function that iterates entities with `std.transform.flat.WorldTransform` and `TextLabel`, and for each visible entity submits a text draw call carrying position, rotation (converted from radians to degrees), font_size, color, and text string to the 2D render pass

#### Scenario: Invisible 2D label is excluded from submission
- **WHEN** `TextLabel.visible = false` on a flat-world entity
- **THEN** the generated body skips the draw submission for that entity

#### Scenario: Rotation is passed as degrees to the raylib draw call
- **WHEN** a flat-world entity has `WorldTransform.rotation = r` (in radians)
- **THEN** the emitted raylib call receives `r * (180.0f / PI)` as the rotation argument

### Requirement: Backend recognizes TextRenderer3D and emits render-to-texture plane rendering
The cpp-entt backend SHALL recognize the `extern rule TextRenderer3D` from `std.render.text` (identified by its filter containing `std.transform.volume.WorldTransform` and `TextLabel`) and emit a rule body that participates in the backend-owned 3D render pass. The implementation SHALL use a per-entity `RenderTexture2D` as the text surface and SHALL draw it as a plane mesh with the entity's full world transform applied via `DrawMesh`.

#### Scenario: TextRenderer3D generates a render-texture-backed draw call
- **WHEN** the backend encounters a recognized `TextRenderer3D` extern rule
- **THEN** it emits a tick function that for each visible entity with `std.transform.volume.WorldTransform` and `TextLabel`: ensures a per-entity `RenderTexture2D` exists, conditionally rebakes the texture if content is dirty, and submits a plane mesh draw to the 3D render pass using the entity's world position, rotation quaternion, and scale

#### Scenario: Render texture is rebaked only when content changes
- **WHEN** a volume-world entity's `TextLabel.text`, `TextLabel.font_size`, or `TextLabel.color` differs from the cached values
- **THEN** the backend executes a bake pass (`BeginTextureMode → ClearBackground → DrawText → EndTextureMode`) before the 3D draw

#### Scenario: Render texture is reused when content is unchanged
- **WHEN** a volume-world entity's `TextLabel` fields have not changed since the previous bake
- **THEN** the backend skips the bake pass and uses the existing `RenderTexture2D` for the draw call

#### Scenario: Invisible 3D label is excluded from draw
- **WHEN** `TextLabel.visible = false` on a volume-world entity
- **THEN** the generated body skips both the bake check and the draw submission for that entity

#### Scenario: Plane mesh orientation follows WorldTransform quaternion
- **WHEN** a volume-world entity has `WorldTransform.rotation` set to a non-identity quaternion
- **THEN** the backend applies the quaternion to the plane mesh draw call via a full 4×4 transform matrix (same `mesh_transform_matrix` pattern as `MeshRenderer`)

### Requirement: Backend manages per-entity RenderTexture2D lifecycle
The cpp-entt backend SHALL allocate `RenderTexture2D` resources lazily on first render and SHALL unload all text render textures during window/runtime teardown alongside other managed render resources.

#### Scenario: Render texture is allocated on first render
- **WHEN** a volume-world entity with `TextLabel` appears for the first time in a frame
- **THEN** the backend allocates a `RenderTexture2D` sized appropriately for the text content and stores it keyed by entity

#### Scenario: All render textures are released at teardown
- **WHEN** the backend runtime tears down (window close or application exit)
- **THEN** all `RenderTexture2D` resources held for text labels are unloaded via `UnloadRenderTexture`

### Requirement: Backend uses a shared plane mesh for all TextRenderer3D draw calls
The cpp-entt backend SHALL allocate a single plane mesh (XY-oriented, 1×1 world units, normal along +Z) shared across all `TextRenderer3D` entities. Per-entity variation SHALL be expressed only through the `DrawMesh` transform matrix and the per-entity bound texture.

#### Scenario: Single plane mesh is allocated once
- **WHEN** one or more volume-world entities with `TextLabel` are present
- **THEN** the backend allocates exactly one plane mesh for all such entities, not one per entity

#### Scenario: Per-entity texture is bound before each draw call
- **WHEN** the backend draws two volume-world `TextLabel` entities with different text in the same frame
- **THEN** each `DrawMesh` call uses the plane mesh but a distinct `RenderTexture2D`-backed material

### Requirement: editor_spawn_template resolves template names at runtime via codegen-emitted registry
`editor_spawn_template` SHALL NOT return `entt::null` for valid `pub template` names declared in the current module. It SHALL consult a `cactus_template_registry` map (emitted by the codegen) to resolve the name to a factory function, create the entity, and patch its position. Previously this function was a stub returning `entt::null` unconditionally.

#### Scenario: Spawn by name succeeds for registered template
- **WHEN** the module declares `pub template Box` and `editor_spawn_template(registry, "Box", {2.0, 1.0}, {})` is called
- **THEN** a new entity is created with all Box template components and `LocalTransform.position = {2.0, 1.0}`
- **THEN** a valid entity handle is returned

#### Scenario: Spawn by unregistered name returns null
- **WHEN** `editor_spawn_template(registry, "DoesNotExist", {}, {})` is called
- **THEN** `entt::null` is returned

### Requirement: editor_hit_test_2d performs world-space AABB hit testing
`editor_hit_test_2d` SHALL convert the screen position to world space using `editor_screen_to_world_2d` and test it against all entities with `WorldTransform` and `BoxCollider`. Previously this function returned `entt::null` unconditionally.

#### Scenario: Hit test finds entity under cursor
- **WHEN** the cursor is at a screen position that maps to world point inside an entity's AABB
- **THEN** `editor_hit_test_2d` returns that entity (not `entt::null`)

#### Scenario: Hit test skips locked entities
- **WHEN** the entity under the cursor has `EditorLocked`
- **THEN** `editor_hit_test_2d` returns `entt::null`

### Requirement: editor_screen_to_world_2d applies camera inverse transform
`editor_screen_to_world_2d` SHALL return the world-space point corresponding to the given screen coordinate, using the active camera set by `set_active_camera_2d`. Previously this function returned the screen coordinate unchanged.

#### Scenario: World-space conversion with active camera
- **WHEN** the active camera has zoom=64 and `offset={400, 300}`
- **THEN** `editor_screen_to_world_2d({400, 300})` returns `{0, 0}`

### Requirement: editor_mouse_delta_2d returns world-space delta
`editor_mouse_delta_2d` SHALL return the mouse pixel delta divided by the active camera zoom, giving world-unit displacement per frame. Previously this function returned zero.

#### Scenario: Delta is non-zero on mouse movement
- **WHEN** the mouse moved 64 pixels to the right and the active camera zoom is 64.0
- **THEN** `editor_mouse_delta_2d()` returns approximately `{1.0, 0.0}`

### Requirement: cpp-entt backend lowers world query expressions to registry queries
The cpp-entt backend SHALL compile recognized `std.query` expressions to direct `entt::registry` queries rather than reflective or name-based lookup logic. The backend SHALL distinguish these from ordinary extern-function calls using semantic metadata that marks the call as a recognized query expression and carries the parsed trait filters.

#### Scenario: Exists query compiles to registry/view check
- **WHEN** authored code uses `query.exists[Boss]()`
- **THEN** the backend generates code that checks whether any live entity currently satisfies the `Boss` filter

#### Scenario: Plain extern func call does not use query lowering
- **WHEN** authored code uses `std.math.sqrt(x)`
- **THEN** the backend lowers it as an ordinary extern-function call and does not attempt registry-query code generation

#### Scenario: Query lowering uses call-site filter metadata
- **WHEN** authored code uses `std.query.count[EnemyAI, not Dead]()`
- **THEN** the backend uses the resolved query callee together with the attached positive and negative trait filters to generate the registry query

#### Scenario: Count query compiles to filtered iteration
- **WHEN** authored code uses `query.count[EnemyAI, not Dead]()`
- **THEN** the backend generates code that counts live registry entities matching the positive and negative trait filter

#### Scenario: Parent query lowers to relationship lookup
- **WHEN** authored code uses `query.parent(of = child_id)`
- **THEN** the backend generates code that looks up the direct parent relationship for `child_id` and returns it as `entity_id`

#### Scenario: Parent query returns stale handle when relationship missing
- **WHEN** authored code uses `query.parent(of = root_id)` and no parent relationship exists
- **THEN** the generated code returns an `entity_id` value for which `registry.valid(id)` is false or which otherwise behaves as stale/non-live under total handle semantics

### Requirement: cpp-entt backend returns stale handle sentinel for empty single-entity queries
For recognized single-entity query expressions such as `first` and `nearest`, the cpp-entt backend SHALL return an `entity_id` value that behaves as stale/non-live when no live entity matches.

#### Scenario: First query on empty result returns stale handle
- **WHEN** `query.first[Boss]()` executes with no live matching entity
- **THEN** the generated code returns an `entity_id` value for which `registry.valid(id)` is false

#### Scenario: Nearest query on empty result returns stale handle
- **WHEN** `query.nearest[Transform, Enemy](from = p)` executes with no live matching entity
- **THEN** the generated code returns an `entity_id` value for which `registry.valid(id)` is false

### Requirement: cpp-entt backend lowers spatial queries to geometry-filtered entity searches
The cpp-entt backend SHALL compile recognized `std.physics.flat.query` and `std.physics.volume.query` expressions to runtime spatial search logic intersected with the provided trait filters.

#### Scenario: Flat nearest query intersects trait filter
- **WHEN** authored code uses `query.nearest[Transform, Enemy](from = p)`
- **THEN** the generated code searches candidate entities using the flat spatial representation and ignores entities missing `Transform` or `Enemy`

#### Scenario: Flat overlap query excludes negative filter matches
- **WHEN** authored code uses `query.overlap_box[Pickup, not Collected](center = p, size = s)`
- **THEN** the generated code excludes any candidate entity that has `Collected`

#### Scenario: Flat circle overlap query lowers to radius-based search
- **WHEN** authored code uses `query.overlap_circle[Enemy](center = p, radius = r)`
- **THEN** the generated code performs a 2D radius-based spatial search and filters matches by the listed traits

#### Scenario: Volume sphere overlap query lowers to radius-based search
- **WHEN** authored code uses `query.overlap_sphere[Enemy](center = p3, radius = r)`
- **THEN** the generated code performs a 3D sphere-overlap search and filters matches by the listed traits

#### Scenario: Raycast query lowers to directional hit search
- **WHEN** authored code uses `query.raycast[Wall, not Trigger](origin = p, dir = d, max_dist = dist)`
- **THEN** the generated code performs a raycast-style search limited by the listed trait filters

### Requirement: cpp-entt backend implements spatial queries via shared runtime helpers
The cpp-entt backend SHALL implement the search logic for `nearest`, `overlap_box`, `overlap_circle`, `overlap_sphere`, and `raycast` spatial queries as shared runtime helper functions rather than duplicating a per-dimension inline search loop at each generated call site. Generated call sites for these queries and for `std.query` expressions (`exists`, `count`, `first`, `all`, `parent`) SHALL NOT use C++ reserved double-leading-underscore identifiers.

#### Scenario: Flat and volume spatial queries share search logic
- **WHEN** the backend lowers a `std.physics.flat.query` or `std.physics.volume.query` spatial query expression
- **THEN** the generated call site invokes a shared runtime helper for that query kind rather than emitting a fully inlined search loop

#### Scenario: Spatial query call sites avoid reserved identifiers
- **WHEN** the backend lowers `nearest`, `overlap_box`, `overlap_circle`, `overlap_sphere`, or `raycast`
- **THEN** the generated call site does not declare or reference a C++ reserved (double-leading-underscore) local identifier

#### Scenario: ECS query call sites avoid reserved identifiers
- **WHEN** the backend lowers a `std.query` expression (`exists`, `count`, `first`, `all`, `parent`)
- **THEN** the generated code does not declare or reference a C++ reserved (double-leading-underscore) local identifier

### Requirement: Editor glue emission is gated on canonical trait identity
The cpp-entt backend SHALL decide whether to emit editor runtime glue — the
`register_editor_hit_test_impl`, `register_editor_spawn_impl`, and
`register_editor_raycast_impl` registrations, the edit-mode HUD overlay, and the viewport
camera translation helpers — using trait lookups that succeed for both canonically-keyed
linked programs (trait map keyed by `std.module.Name`) and simple-name-keyed
single-module programs. Simple-name presence probes that only match the map key SHALL NOT
be used as emission gates.

#### Scenario: Hit-test and spawn impls emitted for a linked 2D editor program
- **WHEN** a multi-module program using `std.editor`, `std.transform.flat`, and
  `std.physics.flat` is linked from artifacts (traits keyed by canonical id) and code is
  generated
- **THEN** the generated `generated_init_project` registers
  `register_editor_hit_test_impl` and `register_editor_spawn_impl`

#### Scenario: Raycast impl emitted for a linked 3D editor program
- **WHEN** a multi-module program using `std.editor`, `std.transform.volume`, and
  `std.render.models` is linked from artifacts and code is generated
- **THEN** the generated `generated_init_project` registers
  `register_editor_raycast_impl`

#### Scenario: Edit-mode overlay emitted for a linked editor program
- **WHEN** any linked program whose merged traits include `std.editor.EditorState` is
  generated
- **THEN** the edit-mode HUD overlay block gated on `EditorState.active` is emitted within
  the render-frame flush boundary — inside `generated_render_project` for the legacy main
  loop, or inside the render phase activation's dispatch for the graph-driven main loop

#### Scenario: Emitted impl lambdas reference resolved component names
- **WHEN** the hit-test, spawn, or raycast impl registration is emitted for a linked
  program
- **THEN** the lambda bodies reference the resolved C++ component type names (e.g.
  `std_transform_flat__WorldTransform`) rather than bare simple names, and the generated
  translation unit compiles

### Requirement: Editor rig dimensionality derives from root-program transform usage
The cpp-entt backend SHALL derive the editor camera rig dimensionality (2D, 3D, or both)
from which `WorldTransform` variant(s) — `std.transform.flat.WorldTransform` or
`std.transform.volume.WorldTransform` — the root module's declarations resolve to. Mere
presence of a variant in the merged trait map SHALL NOT be used, because `std.editor`
transitively imports both variants into every editor program. The selected dimensionality
determines which `camera_enter` rig branch, which viewport camera helper
(`set_active_camera_2d` / `set_active_camera_3d`), and which transform component the
emitted glue uses.

#### Scenario: 3D program generates the 3D rig path and active 3D camera
- **WHEN** a program whose root module entities and templates reference only
  `std.transform.volume.WorldTransform` is generated with `std.editor` and a viewport
- **THEN** the generated `camera_enter` impl contains the 3D rig branch (spawning
  `EditorCamera3D`), and the viewport render loop calls `set_active_camera_3d` for
  viewport entities carrying the volume camera

#### Scenario: 2D program generates the 2D rig path and active 2D camera
- **WHEN** a program whose root module entities and templates reference only
  `std.transform.flat.WorldTransform` is generated with `std.editor` and a viewport
- **THEN** the generated `camera_enter` impl contains the 2D rig branch (spawning
  `EditorCamera2D`), and the viewport render loop calls `set_active_camera_2d`

#### Scenario: Dimensionality is deterministic
- **WHEN** the same program is generated repeatedly
- **THEN** the emitted rig branches and camera helper calls are identical across runs
  (independent of trait map iteration order)

### Requirement: Ambiguous simple-name trait lookups fail loudly in codegen
When a codegen trait lookup by simple name matches two or more traits with different
canonical ids in the merged program, the backend SHALL raise an internal codegen error
identifying the ambiguous name instead of selecting an arbitrary match. Call sites that
can legitimately encounter both stdlib variants SHALL look up by canonical id.

#### Scenario: Ambiguous lookup raises an error
- **WHEN** codegen performs a simple-name trait lookup for a name carried by two traits
  with different canonical ids (e.g. `WorldTransform` with both flat and volume linked)
- **THEN** code generation fails with an internal error naming the ambiguous trait,
  rather than emitting code based on an arbitrary variant

#### Scenario: Unique simple names still resolve
- **WHEN** codegen performs a simple-name lookup for a trait whose name is unique in the
  merged program (e.g. `ModelRenderer`)
- **THEN** the lookup succeeds regardless of whether the map key is the simple name or
  the canonical id

### Requirement: Input bindings emitted from resolved enum member identities
The cpp-entt backend SHALL emit keyboard and mouse binding constants for `input` declarations by consuming the resolved enum member identity attached to each binding property value: members of `std.input.Key` map to raylib `KEY_*` constants (with side-agnostic modifiers mapping to the left-side constants: `Shift` → `KEY_LEFT_SHIFT`, `Ctrl` → `KEY_LEFT_CONTROL`, `Alt` → `KEY_LEFT_ALT`), and members of `std.input.MouseButton` map to raylib `MOUSE_BUTTON_*` constants. The backend SHALL NOT recognize binding constants by source spelling or AST shape. Members of `std.input.GamepadButton` and `std.input.GamepadAxis` are accepted from the frontend but produce no bindings in this backend.

#### Scenario: Alias-qualified key binding emits raylib constant
- **WHEN** an axis input declares `negative = inp.Key.A` and `positive = inp.Key.D` with `use std.input as inp`
- **THEN** the generated axis evaluation reads `KEY_A` and `KEY_D`, not placeholder constants

#### Scenario: Spelling-independent emission
- **WHEN** the same key binding is written `inp.Key.Space` in one program and `std.input.Key.Space` in another
- **THEN** both programs generate identical binding code using `KEY_SPACE`

#### Scenario: Modifier maps to left-side constant
- **WHEN** a button input declares `key = inp.Key.Shift`
- **THEN** the generated binding uses `KEY_LEFT_SHIFT`

#### Scenario: Mouse binding emits mouse constant
- **WHEN** a button input declares `mouse = inp.MouseButton.Left`
- **THEN** the generated mouse binding lookup returns `MOUSE_BUTTON_LEFT`

#### Scenario: Missing resolution fails generation loudly
- **WHEN** an input binding property reaches code generation without a resolved enum member identity
- **THEN** generation fails with an internal error identifying the input declaration, instead of emitting a dead binding such as `0` or `-1`

### Requirement: Generated phase scheduler
The cpp-entt backend SHALL generate execution from the DecoratedProgram phase and handler graph rather than hard-coded lifecycle names or extern-rule name/filter heuristics. It SHALL implement the declared accumulator, max catch-up, phase barriers, stable topological order, and synthesized periodic `dt` and `alpha` fields.

#### Scenario: Standard frame schedule follows declarations
- **WHEN** the standard frame, input, fixed_tick, tick, late_tick, and render declarations are linked
- **THEN** generated code executes their declared activation graph without special-casing rule names

#### Scenario: Fixed catch-up and alpha are generated
- **WHEN** fixed_tick declares `every: 1.0 / 60.0` and `max: 8`
- **THEN** generated code runs at most eight repetitions, drops excess whole steps, preserves the remainder, and computes alpha

### Requirement: Phase field initializer propagation
For each declared phase field with a resolved initializer binding, the backend SHALL emit an assignment in that phase's runtime batch function that writes the bound source value into the phase's runtime-state field before dispatching handlers. A field bound to the runtime root event SHALL be assigned from the batch function's `root_event` parameter. A field bound to an upstream phase SHALL be assigned from that upstream phase's own persisted runtime state. Each assignment SHALL be wrapped in a cast to the declared field's C++ type when the source's underlying storage type differs.

This requirement does not apply to synthesized periodic-phase fields (`dt`, `alpha` on a phase with `every:`), which the backend already populates via accumulator/catch-up bookkeeping.

#### Scenario: Root-event-bound field is assigned before dispatch
- **WHEN** `phase tick` declares `dt: float = frame.dt`
- **THEN** `generated_run_phase_batch_std_core__tick` assigns `phase.dt` from `root_event.dt` before calling `generated_dispatch_phase_std_core__tick`

#### Scenario: Upstream-phase-bound field is assigned before dispatch
- **WHEN** `phase render` declares `alpha: float = fixed_tick.alpha`
- **THEN** `generated_run_phase_batch_std_core__render` assigns `phase.alpha` from the persisted `std_core__fixed_tick` runtime state's `alpha` field before calling `generated_dispatch_phase_std_core__render`

#### Scenario: Synthesized periodic fields are unaffected
- **WHEN** `phase fixed_tick` declares `every: 1.0 / 60.0`
- **THEN** its synthesized `dt` and `alpha` fields continue to be populated by the existing accumulator/catch-up logic, not by initializer-binding assignment

### Requirement: Activation command buffer and event cascade
The cpp-entt backend SHALL buffer spawn, destroy, add, and remove commands during an activation, drain emitted event cascades under the configured depth rule, and apply buffered commands deterministically at that activation's commit boundary. When the linked program has at least one handler triggered by `std.core.spawn` (respectively `std.core.destroy`), commit SHALL emit a `std.core.spawn` (respectively `std.core.destroy`) occurrence back into the same activation's cascade for each applied `Spawn` (respectively `Destroy`) command, subject to the same cascade-depth rule already applied to handler-emitted events. Programs with no handler triggered by `std.core.spawn`/`std.core.destroy` SHALL NOT emit this notification code path.

#### Scenario: Structural command is deferred to commit
- **WHEN** a fixed_tick handler issues `spawn Particle`
- **THEN** the entity is created at the repetition commit and is selectable by the next activation

#### Scenario: Event consumer runs before commit
- **WHEN** a phase handler emits Contact and a Contact handler issues commands
- **THEN** the Contact cascade completes and its commands join the same activation commit

#### Scenario: Commit emits spawn notification when consumed
- **WHEN** the linked program declares a rule with an `on spawn` handler and some other handler issues `spawn Enemy`
- **THEN** after the `Spawn` command is applied at commit, a `std.core.spawn` occurrence re-enters the same activation's cascade and the `on spawn` handler runs against the newly created entity

#### Scenario: Commit emits destroy notification when consumed
- **WHEN** the linked program declares a rule with an `on destroy` handler and some other handler issues `destroy` on an entity
- **THEN** after the `Destroy` command is applied at commit, a `std.core.destroy` occurrence re-enters the same activation's cascade and the `on destroy` handler runs

#### Scenario: No spawn/destroy handler means no notification codegen
- **WHEN** the linked program declares no handler triggered by `std.core.spawn` or `std.core.destroy`
- **THEN** `generated_commit_activation` applies commands without emitting either notification, and no dispatch overload for `std_core__spawnEvent`/`std_core__destroyEvent` is generated

### Requirement: Graph-driven main loop runs one-shot load/unload boundary activations
When the cpp-entt backend generates the graph-driven main loop, and the linked program declares at least one handler triggered by `std.core.load`, the generated `main()` SHALL run one activation — inject a `std.core.load` root occurrence, execute its handler cascade, then commit — exactly once, after `generated_init_project` completes and before the first frame occurrence is injected. Symmetrically, when the linked program declares at least one handler triggered by `std.core.unload`, generated `main()` SHALL run one `std.core.unload` activation (inject, cascade, commit) exactly once, after the frame loop exits and before `CloseWindow()`. Neither activation SHALL execute when the corresponding trigger has no handler in the linked program.

#### Scenario: Load activation runs once before the first frame
- **WHEN** a linked program declares `rule SpawnCharacters: filter: SpawnerMarker` `on load:` that queues three `spawn` commands
- **THEN** generated `main()` runs the load activation (including its commit) after `generated_init_project` and before the loop's first `frame` injection, so all three entities exist for the first frame's phases

#### Scenario: Unload activation runs once at teardown
- **WHEN** the linked program imports `std.core` (providing `SceneCleanup`'s `on unload: destroy`)
- **THEN** generated `main()` runs the unload activation after `WindowShouldClose()` becomes true and before `CloseWindow()`, so non-persistent entities are destroyed through the normal commit path before the window closes

#### Scenario: No load handler means no boot activation
- **WHEN** the linked program declares no handler triggered by `std.core.load`
- **THEN** generated `main()` does not inject a `std.core.load` occurrence or run a boot activation; `generated_init_project` is followed directly by the frame loop

#### Scenario: Legacy main loop is out of scope
- **WHEN** `graph_driven_frame` is false
- **THEN** the boot/teardown activation behavior described here is not emitted; `generated_load_project` remains the pre-existing empty compatibility stub, and legacy-mode `on load`/`on unload` dispatch is unchanged by this requirement (no currently-built example uses the legacy main loop)

### Requirement: Graph-driven main loop executes per-frame render and input housekeeping
When the cpp-entt backend generates the graph-driven main loop (`graph_driven_frame` true — the program has a non-empty execution graph and a resolved external frame event), the generated code SHALL still execute, once per real display frame, the same housekeeping the legacy main loop performs via `generated_update_project`/`generated_render_project`:

- Input-consumption reset (`reset_consumed_input()`) SHALL execute before the input phase activates for that frame occurrence.
- The render-frame flush boundary (`begin_render_frame()` / `end_render_frame()`) SHALL wrap the render phase activation's system dispatch, so render-phase extern rules (mesh/sprite/light/text renderers, the viewport loop, and the editor HUD overlay) submit draw work that reaches the screen.
- Projected-trait cleanup (`clear_projected_traits(registry)`) SHALL execute once per frame after the render phase activation completes.

This SHALL hold regardless of custom phase names — the program's identified render phase (the phase already used to decide which extern rules are render-phase systems) is the one wrapped by the render-frame flush boundary, not necessarily a phase literally named `render`.

#### Scenario: Input reset runs once per frame under the graph-driven loop
- **WHEN** a graph-driven program's main loop injects one frame occurrence
- **THEN** `reset_consumed_input()` executes exactly once before that frame's input phase activation runs

#### Scenario: Render-phase systems draw between begin and end render frame
- **WHEN** a graph-driven program declares a `MeshRenderer`-recognized render-phase extern rule
- **THEN** the generated render phase activation calls `begin_render_frame()`, then the render-phase system dispatch, then `end_render_frame()`, in that order, every frame

#### Scenario: Projected traits are cleared after the render phase completes
- **WHEN** a graph-driven program projects a trait during a frame
- **THEN** `clear_projected_traits(registry)` executes after that frame's render phase activation returns, and the projection is not visible on the next frame unless re-projected

#### Scenario: Legacy main loop is unaffected
- **WHEN** `graph_driven_frame` is false
- **THEN** `generated_update_project`/`generated_render_project` continue to perform this housekeeping exactly as before, and the graph-driven housekeeping described above is not emitted

### Requirement: Contract-shaped external handler lowering
The cpp-entt backend SHALL lower each external handler to a callback or recognized runtime implementation whose const/mutable component access, event emitter, command interface, and effect services are bounded by its declared contract. Selectionless handlers SHALL be invoked once; selected handlers SHALL use typed EnTT views.

#### Scenario: External renderer uses declared render phase
- **WHEN** SpriteRenderer declares `on render`, trait reads, and `effects: graphics`
- **THEN** it executes in the render handler graph without renderer-name scheduling heuristics

#### Scenario: External producer runs once
- **WHEN** selectionless InputSource handles input
- **THEN** generated code calls it once rather than iterating registry entities

### Requirement: cpp-entt backend avoids reserved identifiers for spawn and foreach temporaries
The cpp-entt backend SHALL NOT use C++ reserved double-leading-underscore identifiers (e.g. `__spawned`) for locally-scoped temporaries emitted while lowering spawn expressions or `foreach` statements. Generated temporaries for these constructs SHALL use plain, non-reserved names, with `foreach` snapshot temporaries remaining unique per call site.

#### Scenario: Spawn expression temporaries avoid reserved prefix
- **WHEN** the backend lowers a `spawn` expression, a template-backed spawn with overrides, or a hierarchical spawn with child overrides
- **THEN** the generated entity-handle, existing-component, override-accumulator, and committed-copy temporaries do not use a C++ reserved (double-leading-underscore) name

#### Scenario: Foreach snapshot temporaries avoid reserved prefix
- **WHEN** the backend lowers a `foreach` statement over a list-valued expression
- **THEN** the generated snapshot temporary's name does not use a C++ reserved (double-leading-underscore) prefix
- **AND** the name remains unique per call site so nested or sibling `foreach` statements in the same generated function do not collide

