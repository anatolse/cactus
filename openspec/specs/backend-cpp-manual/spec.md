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
The backend SHALL generate event POD structs and `std::vector`-based event buffers for each declared event. An event dispatch function SHALL flush buffers and invoke registered handlers.

#### Scenario: Event declaration
- **WHEN** the decorated AST contains `event Damage:` with field `amount: int`
- **THEN** the backend generates `struct DamageEvent { int amount; };` and `std::vector<DamageEvent> damage_buffer;`

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
