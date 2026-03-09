## ADDED Requirements

### Requirement: Platformer module structure
The example SHALL consist of separate `.cactus` module files under `examples/platformer/`, each declaring its own `module` and using `use` to import dependencies. The required modules SHALL be: `main`, `player`, `level`, `enemies`, `collectibles`, `camera`, and `ui`.

#### Scenario: All module files exist
- **WHEN** the `examples/platformer/` directory is listed
- **THEN** it SHALL contain `main.cactus`, `player.cactus`, `level.cactus`, `enemies.cactus`, `collectibles.cactus`, `camera.cactus`, and `ui.cactus`

#### Scenario: Each file declares its module
- **WHEN** any `.cactus` file in the platformer example is read
- **THEN** it SHALL begin with a `module <name>` declaration matching its filename (without extension)

#### Scenario: Cross-module imports use `use` declarations
- **WHEN** a module references traits, events, or units from another module
- **THEN** it SHALL include a `use <module>` declaration for each dependency

### Requirement: Main module configuration
The `main.cactus` file SHALL define window configuration constants and import all other modules. It SHALL follow the same pattern as `cactus_shop/main.cactus`.

#### Scenario: Window constants defined
- **WHEN** `main.cactus` is parsed
- **THEN** it SHALL contain a `const` block with at least `WINDOW_TITLE`, `WINDOW_WIDTH`, `WINDOW_HEIGHT`, and `TARGET_FPS`

#### Scenario: All modules imported
- **WHEN** `main.cactus` is parsed
- **THEN** it SHALL contain `use` declarations for `player`, `level`, `enemies`, `collectibles`, `camera`, and `ui`

### Requirement: Player character with platformer mechanics
The `player.cactus` module SHALL define traits and systems for a 2.5D platformer character with run, jump, double-jump, and wall-slide capabilities.

#### Scenario: Player traits defined
- **WHEN** `player.cactus` is parsed
- **THEN** it SHALL contain traits for: `Position` (with `vec2` position and velocity), `PlayerPhysics` (gravity, jump force, ground state), `PlayerController` (movement speed, jump state, facing direction), and `Health` (health points, lives)

#### Scenario: Player unit composes all traits
- **WHEN** the `Player` unit declaration is parsed
- **THEN** its `apply` block SHALL list `Position`, `PlayerPhysics`, `PlayerController`, and `Health`

#### Scenario: Movement system handles horizontal input
- **WHEN** the `MovementSystem` processes a tick
- **THEN** it SHALL read horizontal input, apply movement speed to velocity, and update the player's facing direction

#### Scenario: Gravity system applies downward force
- **WHEN** the `GravitySystem` processes a tick
- **THEN** it SHALL add gravity to the vertical velocity of entities with `PlayerPhysics` and update position based on velocity

#### Scenario: Jump system supports double-jump
- **WHEN** the jump input is pressed and the player has remaining jumps
- **THEN** the system SHALL set upward velocity and decrement the jump counter (max 2 jumps: ground jump + one air jump)

### Requirement: Level structure with platforms
The `level.cactus` module SHALL define platform tiles, ground, and the level layout using traits and units.

#### Scenario: Platform trait defined
- **WHEN** `level.cactus` is parsed
- **THEN** it SHALL contain a `Platform` trait with position (`vec2`), size (`vec2`), and a platform type enum

#### Scenario: Collision detection system
- **WHEN** the `CollisionSystem` processes a tick
- **THEN** it SHALL check AABB overlap between entities with `Position`+`PlayerPhysics` and entities with `Platform`, emitting collision events

#### Scenario: Level units define platform layout
- **WHEN** `level.cactus` is parsed
- **THEN** it SHALL contain at least 3 platform units with different positions and sizes composing the `Platform` trait

### Requirement: Enemy entities with patrol AI
The `enemies.cactus` module SHALL define enemy types with simple back-and-forth patrol behavior.

#### Scenario: Enemy trait defined
- **WHEN** `enemies.cactus` is parsed
- **THEN** it SHALL contain an `EnemyAI` trait with patrol bounds, speed, and direction

#### Scenario: Patrol system moves enemies
- **WHEN** the `PatrolSystem` processes a tick
- **THEN** it SHALL move enemies along their patrol path and reverse direction at bounds

#### Scenario: Enemy-player collision emits damage event
- **WHEN** an enemy overlaps with the player
- **THEN** the system SHALL emit a `PlayerDamaged` event

### Requirement: Collectible items with scoring
The `collectibles.cactus` module SHALL define gem/lum collectible items and a scoring system.

#### Scenario: Collectible trait defined
- **WHEN** `collectibles.cactus` is parsed
- **THEN** it SHALL contain a `Collectible` trait with point value and a collected state

#### Scenario: Collection system detects pickup
- **WHEN** a player entity overlaps with a collectible entity
- **THEN** the system SHALL emit a `GemCollected` event with the point value

#### Scenario: Score tracking
- **WHEN** `collectibles.cactus` is parsed
- **THEN** it SHALL contain a `Score` trait with `persist var score: int` and a system that handles `GemCollected` events to increment the score

### Requirement: Side-scrolling camera
The `camera.cactus` module SHALL define a camera that follows the player horizontally with smooth interpolation and parallax support.

#### Scenario: Camera trait defined
- **WHEN** `camera.cactus` is parsed
- **THEN** it SHALL contain a `SideScrollCamera` trait with offset, smoothing factor, and bounds

#### Scenario: Camera follow system
- **WHEN** the `CameraFollowSystem` processes a tick
- **THEN** it SHALL interpolate the camera position toward the player's X position using the smoothing factor

### Requirement: HUD overlay
The `ui.cactus` module SHALL define a heads-up display showing health, score, and lives.

#### Scenario: HUD view defined
- **WHEN** `ui.cactus` is parsed
- **THEN** it SHALL contain a `view HUD` declaration with parameters for health, score, and lives

#### Scenario: HUD system renders each frame
- **WHEN** the `HUDSystem` processes a tick
- **THEN** it SHALL call `render_view HUD` with current health, score, and lives values

### Requirement: Valid Cactus DSL syntax
All `.cactus` files in the platformer example SHALL be valid according to the Cactus DSL specification. They SHALL use only language features that the compiler supports.

#### Scenario: Files parse without errors
- **WHEN** each `.cactus` file is passed to the cactus compiler
- **THEN** the lexer and parser SHALL produce no errors

#### Scenario: No string literals outside const blocks
- **WHEN** any `.cactus` file is inspected
- **THEN** all string literals SHALL appear only inside `const` blocks, per the const-string rule

#### Scenario: Functions are pure
- **WHEN** any `func` declaration is inspected
- **THEN** it SHALL contain no `emit` statements and no external state mutation
