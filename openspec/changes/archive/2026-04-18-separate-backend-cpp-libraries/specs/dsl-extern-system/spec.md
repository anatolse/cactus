## MODIFIED Requirements

### Requirement: User-defined extern systems generate typed C++ scaffold
When an `extern system` declaration references only non-stdlib traits, the backend SHALL generate typed C++ declarations and scheduling glue for a callback function that the user must implement in the project’s user library. The callback signature is determined by the backend based on the filter traits. The function name follows the convention `<SystemName>_update`.

#### Scenario: User extern system generates C++ library contract
- **WHEN** `extern system MyParticleSystem:` with `filter: Position as pos, ParticleEmitter as pe` is compiled
- **THEN** the backend generates a typed declaration for `MyParticleSystem_update(entt::registry& registry, entt::entity entity, Position& pos, ParticleEmitter& pe)` (or backend-equivalent form) and emits scheduling glue that expects the implementation to come from the user library

#### Scenario: Missing user implementation is a link-time error
- **WHEN** an `extern system` is declared but the user library does not provide the required C++ implementation
- **THEN** the linker reports an undefined reference to `<SystemName>_update`

### Requirement: Stdlib extern systems run automatically from module import
When a module imports a stdlib module that declares `extern system` declarations (e.g., `use std.render.sprites`), those extern systems SHALL be automatically included in the program's system schedule. Their implementations SHALL be satisfied by the selected standard Cactus backend/runtime library rather than by user code.

#### Scenario: SpriteRenderer runs from backend library after module import
- **WHEN** a program imports `std.render.sprites` and applies `std.render.sprites.Renderer` to an entity
- **THEN** `SpriteRenderer` runs each frame without any additional author declaration and its implementation is resolved from the selected standard Cactus backend/runtime library

#### Scenario: Unused extern system is not included
- **WHEN** a program imports `std.render.sprites` but no entity has `std.render.sprites.Renderer` applied
- **THEN** `SpriteRenderer` is not included in the generated system schedule