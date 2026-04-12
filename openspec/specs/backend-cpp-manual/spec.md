## Requirements
### Requirement: SoA storage generation from traits
The backend SHALL generate C++ Structure-of-Arrays (SoA) storage classes for each trait. Each trait field SHALL become a separate `std::vector` in the storage class, enabling cache-friendly iteration.

#### Scenario: Trait with two fields
- **WHEN** the decorated AST contains `trait Position:` with `var x: float` and `var y: float`
- **THEN** the backend generates a C++ struct with `std::vector<float> x;` and `std::vector<float> y;` members

### Requirement: POD struct generation
The backend SHALL generate plain C++ POD structs for each Cactus `struct` declaration.

#### Scenario: Struct with fields
- **WHEN** the decorated AST contains `struct Item:` with fields `price: int` and `weight: float`
- **THEN** the backend generates `struct Item { int price; float weight; };`

### Requirement: System iteration loop generation
The backend SHALL generate system update functions that iterate over SoA storage arrays. `map` operations SHALL produce SIMD-friendly loops; `filter` operations SHALL produce conditional iteration; `reduce` operations SHALL produce accumulation loops.

#### Scenario: Map operation on trait field
- **WHEN** a system contains `positions.map(p => p + velocity * dt)`
- **THEN** the backend generates a `for` loop iterating over the position and velocity SoA vectors

#### Scenario: Filter operation
- **WHEN** a system contains `entities.filter(e => e.health > 0)`
- **THEN** the backend generates a loop with an `if` guard checking the health array

### Requirement: Event buffer generation
The backend SHALL generate event POD structs and `std::vector`-based event buffers for each declared event, including lifecycle events sourced from the std.core AST event declarations. An event dispatch function SHALL flush buffers and invoke registered handlers. For each event handler the backend SHALL bind the event data as `const EventType& <name>` where `<name>` is the handler alias if present, otherwise the event name. The backend SHALL NOT emit individual field parameters (e.g., `float dt`) — handler body code accesses fields via the event variable (`tick.dt`).

#### Scenario: Event declaration generates struct and buffer
- **WHEN** the decorated AST contains `event Damage:` with field `amount: int`
- **THEN** the backend generates `struct DamageEvent { int amount; };` and `std::vector<DamageEvent> damage_buffer;`

#### Scenario: Lifecycle tick event generates struct with dt field
- **WHEN** the std.core `tick` event (field `dt: float`) is processed from AST event declarations
- **THEN** the backend generates `struct TickEvent { float dt; };` (sourced from AST, not a hardcoded list)

#### Scenario: on tick handler body accesses tick.dt
- **WHEN** a handler body references `tick.dt`
- **THEN** the generated C++ handler function receives `const TickEvent& tick` and accesses `tick.dt`

#### Scenario: on tick with alias uses alias name in generated code
- **WHEN** `on tick as t:` is declared
- **THEN** the generated handler function receives `const TickEvent& t` and body references use `t.dt`

#### Scenario: User event handler uses event name as variable
- **WHEN** `on PlayerDamaged:` handler body contains `h.health -= PlayerDamaged.amount`
- **THEN** the generated handler function receives `const PlayerDamagedEvent& PlayerDamaged` and the expression accesses `.amount`

#### Scenario: Marker lifecycle event generates empty struct
- **WHEN** the std.core `spawn` event (no fields) is processed from AST event declarations
- **THEN** the backend generates `struct SpawnEvent {};` and corresponding buffer

### Requirement: Persist field serialization hooks
The backend SHALL generate serialization/deserialization function stubs for fields marked with `persist`. These functions SHALL serialize the field to/from a binary or JSON format.

#### Scenario: Persist field generates save/load
- **WHEN** a trait has `persist var health: int`
- **THEN** the backend generates serialization code that includes `health` in save/load functions

### Requirement: Sync field replication hooks
The backend SHALL generate network replication stubs for fields marked with `sync`. These stubs SHALL mark the field for delta-based network updates.

#### Scenario: Sync field generates replication
- **WHEN** a trait has `sync var position: vec3`
- **THEN** the backend generates replication code that includes `position` in network update functions

### Requirement: Raylib integration in generated code
The backend SHALL generate a main game loop using Raylib API calls by default (InitWindow, BeginDrawing, EndDrawing, CloseWindow). The generated code SHALL be compilable with Raylib linked.

#### Scenario: Generated main loop
- **WHEN** the full pipeline runs on a complete `.cactus` program
- **THEN** the generated C++ code includes Raylib headers and a main function with the standard Raylib game loop pattern

### Requirement: Compilable C++20 output
The backend SHALL produce syntactically and semantically valid C++20 code that compiles with GCC 10+, Clang 10+, or MSVC 19.29+.

#### Scenario: Generated code compiles
- **WHEN** the backend generates code from the cactus shop mini example
- **THEN** the output compiles without errors using a C++20 compiler with Raylib and standard library available

### Requirement: Emit `cactus_runtime.h` include when extern funcs are in scope
The cpp-manual backend SHALL emit `#include "cactus_runtime.h"` in the generated C++ output when any extern func is present — either declared in the program itself or in any imported module's `ImportedSymbols.funcs` map (where `is_extern = true`).

The include SHALL be placed in the standard include block near the top of the generated file, after the fixed system headers and before the generated type definitions.

#### Scenario: Runtime header emitted when module imports std.input
- **WHEN** a program imports `std.input` (which declares extern funcs) and is compiled with cpp-manual
- **THEN** the generated file contains `#include "cactus_runtime.h"` in its include section

#### Scenario: Runtime header not emitted for extern-free programs
- **WHEN** a program contains no extern funcs and imports no modules with extern funcs
- **THEN** the generated file does NOT contain `#include "cactus_runtime.h"`

### Requirement: No C++ body emitted for extern funcs
The cpp-manual backend SHALL NOT emit a C++ function definition for any `FuncNode` or `ResolvedFunc` entry where `is_extern = true`. Extern funcs are satisfied by `cactus_runtime.h` and require no generated body.

#### Scenario: Extern func produces no generated function body
- **WHEN** the program declares `pub extern func pressed(b: InputButton) bool`
- **THEN** the generated C++ does NOT contain a definition `bool pressed(InputButton b) { ... }`

### Requirement: `self` compiles to the current manual-backend entity slot
The manual backend SHALL compile `self` to the entity currently being processed by the generated handler. Where entity identity is represented separately from storage slots, the generated code SHALL use the current entity identity corresponding to the active slot.

#### Scenario: `self` used as destroy target in manual backend
- **WHEN** `destroy self` is compiled
- **THEN** the generated code removes the current entity without requiring a separate target expression

#### Scenario: `self` written into parent field
- **WHEN** `Parent.parent = self` is compiled
- **THEN** the generated code writes the current entity identity into the `parent` field storage

### Requirement: manual backend implements hierarchy propagation and cascade deletion
The manual backend SHALL generate runtime support for hierarchy transform propagation and recursive descendant deletion.

#### Scenario: propagation updates world transform arrays
- **WHEN** hierarchy propagation is emitted for entities with `Parent`, `LocalTransform`, and `WorldTransform`
- **THEN** the generated code computes and stores derived `WorldTransform` field values in SoA storage

#### Scenario: cascade deletion removes descendant subtree
- **WHEN** a parent entity is destroyed in the manual backend
- **THEN** the generated code destroys all descendants recursively before the hierarchy subtree is fully removed

