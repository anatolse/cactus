## ADDED Requirements

### Requirement: Self-contained CMake sub-project
The `examples/platformer/game/` directory SHALL contain a standalone CMake project that builds a runnable platformer executable. It SHALL be buildable independently from the root cactus compiler project.

#### Scenario: CMakeLists.txt exists and is valid
- **WHEN** `examples/platformer/game/CMakeLists.txt` is opened
- **THEN** it SHALL be a valid CMake project with `cmake_minimum_required`, `project()`, and at least one executable target

#### Scenario: Project builds independently
- **WHEN** the sub-project is configured with `-DCACTUS_COMPILER=<path>`
- **THEN** CMake SHALL configure successfully without requiring the root project's build tree

### Requirement: Cactus compiler integration via custom commands
The CMake project SHALL invoke the cactus compiler as a build step to generate C++ source from `.cactus` files.

#### Scenario: Custom command per cactus file
- **WHEN** the CMake project is configured
- **THEN** it SHALL define an `add_custom_command` for each `.cactus` file in `examples/platformer/` that runs `${CACTUS_COMPILER} <file>.cactus --backend cpp-entt -o <output>.generated.h`

#### Scenario: Generated files are build dependencies
- **WHEN** a `.cactus` file is modified and the project is rebuilt
- **THEN** the corresponding `add_custom_command` SHALL re-run, regenerating the C++ header

#### Scenario: CACTUS_COMPILER variable required
- **WHEN** the CMake project is configured without `-DCACTUS_COMPILER`
- **THEN** CMake SHALL emit a `FATAL_ERROR` message explaining that the cactus compiler path must be provided

### Requirement: Raylib and EnTT dependencies
The sub-project SHALL fetch Raylib and EnTT via FetchContent so it is self-contained.

#### Scenario: FetchContent declarations
- **WHEN** `CMakeLists.txt` is parsed
- **THEN** it SHALL contain `FetchContent_Declare` for both `raylib` and `EnTT` with pinned version tags

#### Scenario: Executable links against dependencies
- **WHEN** the executable target is defined
- **THEN** it SHALL link against `raylib` and `EnTT::EnTT`

### Requirement: Hand-written main.cpp game loop
The sub-project SHALL include a `main.cpp` that provides the Raylib game loop boilerplate and includes the generated headers.

#### Scenario: main.cpp exists
- **WHEN** `examples/platformer/game/main.cpp` is read
- **THEN** it SHALL contain `#include` directives for the generated `.generated.h` files

#### Scenario: Raylib initialization
- **WHEN** `main.cpp` is compiled and run
- **THEN** it SHALL call `InitWindow`, `SetTargetFPS`, run a `while (!WindowShouldClose())` loop with `BeginDrawing`/`EndDrawing`, and call `CloseWindow`

#### Scenario: ECS registry setup
- **WHEN** `main.cpp` initializes
- **THEN** it SHALL create an `entt::registry`, register components from the generated code, and call system update functions each frame

### Requirement: Build instructions in README
The sub-project SHALL include a `README.md` with build instructions.

#### Scenario: README exists
- **WHEN** `examples/platformer/game/README.md` is read
- **THEN** it SHALL contain step-by-step instructions for building the cactus compiler first, then configuring and building the game sub-project

#### Scenario: Example commands provided
- **WHEN** the README is read
- **THEN** it SHALL include example `cmake` configure and build commands with the `-DCACTUS_COMPILER=` flag
