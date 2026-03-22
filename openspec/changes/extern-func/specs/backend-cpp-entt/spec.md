## ADDED Requirements

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
- **WHEN** the program declares `pub extern func lerp(a, b, t: float) -> float`
- **THEN** the generated C++ does NOT contain a definition `float lerp(float a, float b, float t) { ... }`
