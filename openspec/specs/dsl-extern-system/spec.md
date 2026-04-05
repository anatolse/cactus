# dsl-extern-system Specification

## Purpose
TBD - created by archiving change dsl-extern-system. Update Purpose after archive.
## Requirements
### Requirement: `extern system` top-level declaration
The DSL SHALL support `extern system Name:` as a top-level declaration. An `extern system` declares a system whose implementation is provided by the backend or by the user in C++. It has the same structural clauses as a regular `system` declaration — `filter:`, `exclude:`, `order by:`, `after:` — but MUST NOT have event handlers. An `extern system` with event handlers SHALL be a compile-time error.

```ebnf
extern_system_decl = "extern" "system" IDENTIFIER ":"
                     INDENT
                       [filter_clause]
                       [exclude_clause]
                       [order_by_clause]
                       [after_clause]
                     DEDENT ;
```

#### Scenario: extern system with filter and order by parsed
- **WHEN** an `extern system SpriteRenderer:` declaration with `filter:` and `order by:` clauses appears
- **THEN** the parser produces an `ExternSystemNode` with the given filter and sort keys

#### Scenario: extern system with no handlers is valid
- **WHEN** an `extern system` declaration has filter and ordering but no `on event:` handlers
- **THEN** the semantic analyzer accepts it

#### Scenario: extern system with handlers is invalid
- **WHEN** an `extern system` declaration contains an `on tick:` handler
- **THEN** the parser or semantic analyzer SHALL report: "`extern system` cannot have event handlers; use `system` instead"

#### Scenario: extern system filter is required
- **WHEN** an `extern system` has no `filter:` clause
- **THEN** the semantic analyzer SHALL report: "`extern system` requires a `filter:` clause (no-filter extern systems are not supported)"

### Requirement: Stdlib extern systems run automatically from module import
When a module imports a stdlib module that declares `extern system` declarations (e.g., `use std.render.sprites`), those extern systems are automatically included in the program's system schedule. Authors do NOT need to re-declare them; applying the relevant traits to entities is sufficient.

#### Scenario: SpriteRenderer runs automatically
- **WHEN** a program imports `std.render.sprites` and applies `std.render.sprites.Renderer` to an entity
- **THEN** `SpriteRenderer` runs each frame without any additional author declaration

#### Scenario: Unused extern system is not included
- **WHEN** a program imports `std.render.sprites` but no entity has `std.render.sprites.Renderer` applied
- **THEN** `SpriteRenderer` is not included in the generated system schedule (the filter matches zero entities; the backend MAY omit the system entirely as an optimization)

### Requirement: User-defined extern systems generate typed C++ scaffold
When an `extern system` declaration references only non-stdlib traits, the backend generates a C++ header with a typed callback function that the user must implement. The callback signature is determined by the backend based on the filter traits. The function name follows the convention `<SystemName>_update`.

#### Scenario: User extern system generates C++ header
- **WHEN** `extern system MyParticleSystem:` with `filter: Position as pos, ParticleEmitter as pe` is compiled
- **THEN** the backend generates a C++ header declaring `void MyParticleSystem_update(entt::registry& registry, entt::entity entity, Position& pos, ParticleEmitter& pe)` (or equivalent batch form at backend's discretion)

#### Scenario: Missing user implementation is a link-time error
- **WHEN** an `extern system` is declared but the user does not provide the C++ implementation
- **THEN** the linker reports an undefined reference to `<SystemName>_update`

### Requirement: `extern system` participates in the system ordering graph
An `extern system` MAY declare `after:` constraints naming other systems (regular or extern). The compiler validates that no cycles exist. The backend schedules extern systems in the same phase graph as regular systems.

#### Scenario: extern system after regular system
- **WHEN** `extern system MeshRenderer: after: TransformSystem` is declared
- **THEN** `MeshRenderer` is scheduled after `TransformSystem` in the same phase

#### Scenario: cycle involving extern system is an error
- **WHEN** `extern system A: after: B` and `system B: after: A` creates a cycle
- **THEN** the semantic analyzer SHALL report: "system ordering cycle detected: A → B → A"

