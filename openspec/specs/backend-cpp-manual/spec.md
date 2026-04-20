## Purpose
Define the required behavior of the manual C++ backend, including code generation, runtime integration, and correctness expectations for generated output.
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
The backend SHALL support a standard runtime-driven game loop for manual-backend projects. The default compiled-project integration SHALL be realized through generated project glue linked against the standard Cactus manual backend/runtime library rather than requiring generated output to embed a complete standalone `main()` implementation.

#### Scenario: Linked manual runtime integration
- **WHEN** the full pipeline runs on a complete `.cactus` program targeting the manual backend
- **THEN** the generated project-specific output links against the standard Cactus manual backend/runtime library, which provides the reusable runtime/game-loop integration for the project

### Requirement: Compilable C++20 output
The backend SHALL produce syntactically and semantically valid C++20 generated project glue that compiles and links successfully with Raylib, the standard library, and the standard Cactus manual backend/runtime library.

#### Scenario: Generated manual project links successfully
- **WHEN** the backend generates C++ from a supported example or authored project
- **THEN** the generated project-specific C++ compiles without errors and links successfully when combined with the standard Cactus manual backend/runtime library and required dependencies

#### Scenario: Curated example compilation coverage validates linked manual projects
- **WHEN** automated example-compilation integration coverage runs for the manual backend path
- **THEN** generated C++ project glue compiles successfully and links against the configured C++ toolchain and the standard Cactus manual backend/runtime library

### Requirement: manual backend provides complete stdlib extern function coverage
The cpp-manual backend and its linked runtime/library SHALL provide concrete implementations or binding paths for stdlib-declared extern functions that are part of the supported stdlib surface and are not blocked by missing language/runtime features, including scalar math, vec2/vec3 helpers, quaternion helpers, and input query functions.

Pure helpers that do not depend on runtime state SHALL use allocation-free implementations and SHALL be declared `constexpr` and `noexcept` where permitted by the underlying C++ operations.

#### Scenario: Math extern declarations resolve through runtime library
- **WHEN** a program imports stdlib modules that declare supported math/vector/quaternion extern functions and is compiled with cpp-manual
- **THEN** the generated project links successfully and the extern calls resolve through concrete cpp-manual/shared runtime-library implementations

#### Scenario: Input extern declarations resolve through manual runtime adapter
- **WHEN** a program imports `std.input` and is compiled with cpp-manual
- **THEN** the generated project links successfully and the input extern calls resolve through concrete cpp-manual runtime adapter implementations

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
The manual backend SHALL provide runtime support for hierarchy transform propagation and recursive descendant deletion through the standard Cactus manual backend/runtime library rather than duplicating backend-generic implementations into every generated project output.

#### Scenario: propagation support comes from backend library
- **WHEN** hierarchy propagation is required for entities with `Parent`, `LocalTransform`, and `WorldTransform`
- **THEN** the generated manual-backend project binds to the standard Cactus manual backend/runtime library implementation that computes and stores derived `WorldTransform` field values

#### Scenario: cascade deletion support comes from backend library
- **WHEN** a parent entity is destroyed in a generated manual-backend project
- **THEN** descendant subtree removal is performed by standard Cactus manual backend/runtime support rather than a project-local duplicated backend implementation

### Requirement: User-defined extern system integration uses a user library
For external behavior that is not provided by the standard Cactus manual backend/runtime library, generated manual-backend project output SHALL declare the required user callback contracts and SHALL expect their implementations to be supplied by the project’s user library.

#### Scenario: Custom extern behavior resolves from user library
- **WHEN** a manual-backend project contains user-defined extern integration points
- **THEN** the generated output declares those integration points and the final linked project resolves them from the user library

### Requirement: manual backend recognizes the full supported stdlib extern system set
The cpp-manual backend SHALL recognize supported stdlib extern system patterns and bind them to backend-library implementations rather than treating them as generic user extern systems.

The supported recognized set SHALL include, at minimum, the stdlib extern system patterns whose required language/runtime features are already implemented, such as hierarchy propagation, cascade deletion, sprite rendering, animated sprite advancement, mesh rendering, point-light registration, and directional-light registration where applicable.

#### Scenario: Supported stdlib renderer binds to backend library
- **WHEN** a recognized stdlib render extern system is compiled with cpp-manual
- **THEN** generated output binds to the corresponding cpp-manual backend-library implementation rather than user-library callback scaffolding

#### Scenario: Supported hierarchy behavior binds to backend library
- **WHEN** a recognized stdlib hierarchy extern system is compiled with cpp-manual
- **THEN** generated output binds to the corresponding cpp-manual backend-library implementation rather than project-local traversal logic

### Requirement: manual backend runtime-owned dynamic storage uses pmr containers
When the cpp-manual backend/runtime requires dynamic storage for performance-critical generated runtime behavior, it SHALL use `std::pmr` containers/resources rather than default-allocator standard containers, unless the implementation is allocation-free.

#### Scenario: Temporary backend-owned collections use pmr
- **WHEN** cpp-manual runtime/library code creates dynamic scratch collections for stdlib-owned backend behavior
- **THEN** those collections use `std::pmr` storage or an allocation-free equivalent

### Requirement: manual backend stdlib coverage is tested behaviorally
The cpp-manual backend SHALL include tests that verify stdlib extern function correctness, recognized extern system binding, and representative runtime behavior for supported stdlib backend features.

#### Scenario: Extern function behavior is tested
- **WHEN** the cpp-manual backend test suite runs
- **THEN** it includes behavioral tests for supported stdlib math/vector/quaternion/input extern functions

#### Scenario: Recognized extern system behavior is tested
- **WHEN** the cpp-manual backend test suite runs
- **THEN** it includes tests covering recognized hierarchy and render extern system behavior or binding outcomes

