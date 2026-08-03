# example-blue-square Specification

## Purpose
TBD - created by archiving change blue-square-wasd-demo. Update Purpose after archive.
## Requirements
### Requirement: Blue square Cactus module
The example SHALL provide a single `examples/blue-square/square.cactus` file that is valid Cactus DSL. It SHALL declare a `Position` trait with `x: float` and `y: float` fields, a `SPEED` constant, a `Square` unit applying `Position`, and a `MoveSystem` that reads WASD input and updates position on each `tick(dt)`.

#### Scenario: Module compiles without errors
- **WHEN** the compiler is invoked with `cactus square.cactus --backend cpp-entt -o square.generated.h`
- **THEN** the command exits with code 0 and produces a valid C++ header

#### Scenario: MoveSystem updates position on W key
- **WHEN** the W key is held and `MoveSystem` executes with `dt = 0.016`
- **THEN** the entity's `y` field decreases by `SPEED * dt` (moves upward in screen space)

#### Scenario: MoveSystem updates position on S key
- **WHEN** the S key is held and `MoveSystem` executes with `dt = 0.016`
- **THEN** the entity's `y` field increases by `SPEED * dt` (moves downward)

#### Scenario: MoveSystem updates position on A key
- **WHEN** the A key is held and `MoveSystem` executes with `dt = 0.016`
- **THEN** the entity's `x` field decreases by `SPEED * dt` (moves left)

#### Scenario: MoveSystem updates position on D key
- **WHEN** the D key is held and `MoveSystem` executes with `dt = 0.016`
- **THEN** the entity's `x` field increases by `SPEED * dt` (moves right)

### Requirement: C++ host application
The example SHALL provide `examples/blue-square/game/main.cpp` that initialises a Raylib window (800×600, titled "Blue Square"), runs a standard game loop at 60 FPS, clears the background white each frame, and draws a 50×50 cornflower-blue rectangle at the entity's current position. It SHALL use the EnTT registry pattern. It SHALL include `#if __has_include("square.generated.h")` guards and fall back to a stub entity and stub movement rule when the generated header is absent.

#### Scenario: Window opens with white background
- **WHEN** the executable is launched
- **THEN** an 800×600 window titled "Blue Square" appears with a white background

#### Scenario: Blue square visible at start
- **WHEN** the window first renders
- **THEN** a 50×50 cornflower-blue (100, 149, 237) rectangle is visible near the centre of the screen

#### Scenario: Square moves when WASD held
- **WHEN** the user holds the D key for one second
- **THEN** the square has moved approximately 200 pixels to the right

#### Scenario: Stub fallback compiles without generated header
- **WHEN** `square.generated.h` does not exist
- **THEN** the project still compiles and runs with stub stub entity behaviour

### Requirement: CMake build configuration
The example SHALL provide `examples/blue-square/game/CMakeLists.txt` that defines a standalone CMake project named `blue_square`, fetches Raylib via `FetchContent` if not already available, links against `raylib` and `EnTT::entt`, and produces an executable named `blue_square`.

#### Scenario: Builds in isolation
- **WHEN** `cmake -S examples/blue-square/game -B build/blue-square` is run followed by `cmake --build`
- **THEN** the build succeeds and a `blue_square` executable is produced

### Requirement: Example README
The example SHALL provide `examples/blue-square/game/README.md` that documents prerequisites (CMake, a C++17 compiler), the two-command build procedure, and the controls (WASD to move, Escape or close window to quit).

#### Scenario: README contains build instructions
- **WHEN** a developer reads `README.md`
- **THEN** they can build and run the example following only the instructions in that file without consulting any other document

