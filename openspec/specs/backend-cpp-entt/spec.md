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
The backend SHALL generate system functions that use `entt::registry::view<Components...>()` for entity iteration. Filter clauses SHALL map to view template parameters.

#### Scenario: System with filter
- **WHEN** a system has `filter:` listing `Position` and `Velocity`, and an `on tick(dt: float):` handler
- **THEN** the backend generates a function using `registry.view<Position, Velocity>().each([](auto& pos, auto& vel) { ... })`

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
