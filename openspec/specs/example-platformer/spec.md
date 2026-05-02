# example-platformer Specification

## Purpose

This spec defines the curated platformer example as a current-stdlib Cactus DSL sample that generates and compiles through the cpp-entt backend.

## Requirements

### Requirement: Platformer example source uses current 2D stdlib authoring patterns
The repository SHALL provide `examples/platformer/platformer.cactus` as a valid Cactus DSL example that demonstrates a 2D platformer-style scene using currently shipped stdlib modules and language syntax.

#### Scenario: Platformer has one authoritative curated DSL source
- **WHEN** users inspect `examples/platformer/` for the curated platformer DSL source
- **THEN** `examples/platformer/platformer.cactus` is the single authoritative curated source
- **AND** the sample does not also present parallel active `.cactus` files such as `player.cactus`, `level.cactus`, `camera.cactus`, or `ui.cactus` as an alternate implementation of the same platformer

#### Scenario: Example imports current stdlib modules
- **WHEN** `examples/platformer/platformer.cactus` is read
- **THEN** it imports `std.input`
- **AND** it imports `std.transform.flat`
- **AND** it imports at least one shipped 2D render module such as `std.render.shapes` or `std.render.sprites`

#### Scenario: Example declares 2D entities through stdlib transform data
- **WHEN** platformer scene units are declared
- **THEN** moving and renderable 2D entities use `std.transform.flat.WorldTransform` or an imported alias of that trait for authored/world position data

#### Scenario: Example uses passive stdlib render traits
- **WHEN** platformer-visible entities such as the player, platforms, enemies, or collectibles are declared
- **THEN** they use shipped passive render traits such as `std.render.shapes.Shape` or `std.render.sprites.Renderer`
- **AND** the example does not require a project-local `draw_rect` extern function to render those entities

### Requirement: Platformer example avoids undeclared backend helper functions
The platformer example SHALL NOT rely on helper functions that are neither declared as Cactus extern functions nor provided by the current stdlib/backend contract.

#### Scenario: No undeclared input helpers are required
- **WHEN** the platformer reads player input
- **THEN** it uses declared Cactus `input` actions and `std.input` query functions rather than an undeclared `is_jump_pressed` helper

#### Scenario: No undeclared cross-entity helpers are required
- **WHEN** platformer systems are semantically analyzed
- **THEN** they do not require undeclared helpers such as `get_player_position`, `get_player_width`, `get_player_height`, or `get_player_entity`

#### Scenario: No undeclared camera helper is required
- **WHEN** platformer camera or rendering behavior is semantically analyzed
- **THEN** it does not require an undeclared `set_camera_2d` helper

### Requirement: Platformer example is compatible with cpp-entt generation
The platformer example SHALL generate cpp-entt project glue that links against the standard cpp-entt runtime/backend library.

#### Scenario: Platformer code generation succeeds
- **WHEN** the compiler is invoked with `cactus examples/platformer/platformer.cactus --backend cpp-entt --output <generated.cpp>`
- **THEN** code generation succeeds without duplicate `std.core` symbol diagnostics

#### Scenario: Platformer generated output uses project glue shape
- **WHEN** the platformer generated output is compiled in curated example coverage
- **THEN** it compiles as project-specific generated C++ linked with the standard cpp-entt runtime/backend library