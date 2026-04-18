## MODIFIED Requirements

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