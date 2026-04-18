## MODIFIED Requirements

### Requirement: Raylib integration in generated code
The backend SHALL support a standard runtime-driven game loop for EnTT projects, with EnTT registry and dispatcher initialized before frame execution. The default compiled-project integration SHALL be realized through generated project glue linked against the standard Cactus EnTT backend/runtime library rather than requiring the generated output to embed a complete standalone `main()` implementation.

#### Scenario: Linked EnTT runtime integration
- **WHEN** the full pipeline runs on a complete `.cactus` program with the EnTT backend selected
- **THEN** the generated project-specific output links against the standard Cactus EnTT backend/runtime library, which provides the reusable runtime/game-loop integration for the project

### Requirement: Compilable C++20 output with EnTT
The backend SHALL produce valid C++20 generated project glue that compiles and links successfully with EnTT v3.x headers, Raylib, and the standard Cactus EnTT backend/runtime library.

#### Scenario: Generated EnTT project links successfully
- **WHEN** the backend generates C++ from a supported example or authored project
- **THEN** the generated project-specific C++ compiles without errors and links successfully when combined with the standard Cactus EnTT backend/runtime library and required dependencies

#### Scenario: Curated example compilation coverage validates linked EnTT projects
- **WHEN** automated example-compilation integration coverage runs for an example configured for the EnTT backend path
- **THEN** the generated C++ project glue compiles successfully and links against the project's configured EnTT-enabled toolchain and the standard Cactus EnTT backend/runtime library

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

#### Scenario: hierarchy propagation binds to backend library implementation
- **WHEN** the stdlib propagation extern system references `Parent`, `std.transform.flat.LocalTransform`, and `std.transform.flat.WorldTransform`
- **THEN** the generated EnTT project output binds to the standard Cactus EnTT backend/runtime library implementation for hierarchy propagation

#### Scenario: Non-stdlib traits do not use backend-library stdlib binding
- **WHEN** `extern system MySystem: filter: Position as pos, MyCustomTrait as c` is compiled and `MyCustomTrait` is user-defined
- **THEN** the backend does not treat the system as a stdlib backend-library implementation

### Requirement: User-defined extern system codegen — C++ scaffold
For `extern system` declarations with user-defined (non-stdlib) traits, the EnTT backend SHALL generate typed declarations and scheduling glue that call user-provided callback implementations from the project’s user library. The backend SHALL generate the iteration infrastructure (including `order by:` support when present), and the final linked project SHALL obtain the callback implementation from the user library rather than from a generated implementation body.

The callback name convention remains `<SystemName>_update`. The signature includes typed references to all filter components.

#### Scenario: User extern system generates library-facing declaration
- **WHEN** `extern system MyParticleSystem: filter: Position as pos, ParticleEmitter as pe` is compiled
- **THEN** the generated EnTT project output declares a typed `MyParticleSystem_update(...)` callback contract and emits scheduling glue that expects the implementation to be supplied by the user library

#### Scenario: order by in user extern system still generates sort call
- **WHEN** a user `extern system` has `order by: pos.pos.y asc`
- **THEN** the generated EnTT scheduling glue includes a `registry.sort<Position>(...)` call before invoking the user-library callback for each matched entity