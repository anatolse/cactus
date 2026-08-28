# example-first-person-arena Specification

## Purpose

Define a compact first-person arena shooter example that demonstrates how Cactus gameplay constructs and existing 3D stdlib surfaces compose into a complete playable loop.

## Requirements

### Requirement: First-person arena example source and assets exist
The repository SHALL provide examples/first-person-arena/main.cactus as a standalone cpp-entt example. The example SHALL reuse the robot and knight GLB files under examples/model-renderer/art/ and SHALL add no new binary assets.

#### Scenario: Example source is present
- **WHEN** the repository examples are inspected
- **THEN** examples/first-person-arena/main.cactus exists as the example entrypoint

#### Scenario: Character assets are reused
- **WHEN** the example's model asset declarations are inspected
- **THEN** they reference the existing robot and knight GLB files
- **AND** the first-person-arena directory contains no copied or newly generated binary model assets

### Requirement: Arena geometry is authored in Cactus
The example SHALL author a roofless square arena in Cactus entities and templates. It SHALL include a dark-grey floor, light-grey perimeter walls, a brown central cubic building with two opposite ground-level door openings, an exterior staircase, and a walkable building roof. The active viewport SHALL provide a blue sky background.

#### Scenario: Arena has no roof
- **WHEN** the authored map entities are inspected
- **THEN** perimeter walls and a floor enclose the play area
- **AND** no arena-covering roof entity blocks the blue sky background

#### Scenario: Central building can be entered and climbed
- **WHEN** the central building geometry is inspected
- **THEN** two opposite wall faces contain door-sized openings
- **AND** an exterior sequence of steps reaches the building's walkable top

#### Scenario: Map colors match the arena theme
- **WHEN** one frame is rendered
- **THEN** the floor is dark grey, perimeter walls are light grey, the building and stairs are brown, and the viewport background is blue

### Requirement: Player uses first-person controls and a collider
The player SHALL move with W/A/S/D input relative to its horizontal facing direction and look with mouse delta using yaw and pitch. Pitch SHALL be clamped to avoid flipping. The first-person camera SHALL remain attached to the player body, the mouse cursor SHALL be captured during play, and the player body SHALL carry volume Collider and CapsuleCollider traits used by authored arena collision and grounding rules.

#### Scenario: Mouse movement rotates the view
- **WHEN** the captured mouse reports horizontal and vertical delta while the game is active
- **THEN** player yaw and camera pitch change using the authored sensitivity
- **AND** pitch remains inside the authored clamp range

#### Scenario: Movement follows facing direction
- **WHEN** forward input is held after the player has yawed ninety degrees
- **THEN** the player advances along the corresponding world-space horizontal facing direction rather than a fixed world axis

#### Scenario: Player cannot cross solid map geometry
- **WHEN** player movement would overlap a perimeter wall or solid building box
- **THEN** authored collision response separates the player from that box

#### Scenario: Player can climb to the building roof
- **WHEN** the player moves over the exterior steps in order
- **THEN** authored grounding raises the player by each reachable step
- **AND** the player can stand and move on the building roof

### Requirement: Four corner spawn points produce robot and knight waves
The arena SHALL contain four authored corner spawn-point entities: two assigned to robots and two assigned to knights. Each spawn point SHALL create one enemy immediately when play begins and one additional enemy every ten seconds while the game remains active.

#### Scenario: Initial wave contains four enemies
- **WHEN** the initial spawn activation commits
- **THEN** two robot enemies and two knight enemies exist
- **AND** each originated at its assigned corner spawn point

#### Scenario: Ten-second interval creates another wave
- **WHEN** ten active gameplay seconds elapse after a wave
- **THEN** every spawn point creates one enemy of its assigned kind

#### Scenario: Game over stops spawning
- **WHEN** the player has entered the game-over state
- **THEN** later spawn intervals create no additional enemies

### Requirement: Live enemies seek the player and end the game on contact
Robot and knight enemies SHALL render their reused models with animation, carry volume Collider and CapsuleCollider traits, face and move toward the live player, and use authored solid-box separation to remain inside the arena and respond to the building. A live enemy reaching the player's collider SHALL trigger game over.

#### Scenario: Enemy advances toward player
- **WHEN** a live enemy and player are separated with no solid box between them
- **THEN** successive fixed updates reduce their horizontal distance

#### Scenario: Enemy responds to a solid box
- **WHEN** an enemy's attempted movement overlaps a wall or building box
- **THEN** authored separation prevents penetration while preserving any unblocked tangential movement

#### Scenario: Enemy contact triggers game over
- **WHEN** a live enemy overlaps the player's collider
- **THEN** the player enters game over exactly once

### Requirement: Player fires small cubic projectiles
Primary mouse input SHALL spawn a small cube-rendered bullet from the first-person camera along its current forward direction, subject to a short authored cooldown. Each bullet SHALL carry velocity, finite lifetime, and a volume box collider. It SHALL be destroyed when its lifetime expires, when it hits solid map geometry, or when it hits a live enemy.

#### Scenario: Shot follows camera aim
- **WHEN** primary fire is pressed while the cooldown permits a shot
- **THEN** one cubic bullet spawns in front of the camera
- **AND** its velocity is parallel to the camera's current forward direction

#### Scenario: Bullet hitting a live enemy is consumed
- **WHEN** a bullet overlaps a live robot or knight collider
- **THEN** the enemy receives one targeted hit occurrence
- **AND** the bullet is destroyed

#### Scenario: Unused bullet expires
- **WHEN** a bullet reaches the end of its authored lifetime without a hit
- **THEN** it is destroyed

### Requirement: Hit enemies fall, fade, and disappear in one second
The first hit on a live robot or knight SHALL transition it to a non-threatening dying state. During the next one second the enemy SHALL stop seeking and animating, rotate from upright toward a fallen pose, and reduce its model alpha continuously from opaque to transparent. At one second the enemy SHALL be destroyed.

#### Scenario: Hit begins the dying transition
- **WHEN** a live enemy receives a bullet hit
- **THEN** it stops participating in player-contact game-over detection and active enemy movement
- **AND** its death timer begins at zero

#### Scenario: Mid-transition enemy is fallen and translucent
- **WHEN** approximately half a second of the death transition has elapsed
- **THEN** the enemy's rotation has progressed toward the fallen pose
- **AND** its model tint alpha is approximately one half

#### Scenario: Transition completes after one second
- **WHEN** the dying timer reaches one second
- **THEN** the enemy entity is destroyed

### Requirement: Game over is visible and terminal
The example SHALL render a centered crosshair during play. When a live enemy reaches the player, it SHALL show a window-space GAME OVER label, stop player movement and firing, stop enemy movement and spawning, and release cursor capture.

#### Scenario: Crosshair is visible during play
- **WHEN** the game is active
- **THEN** a crosshair is rendered at the center of the window

#### Scenario: Game-over presentation activates
- **WHEN** enemy contact triggers game over
- **THEN** GAME OVER is visible in window space
- **AND** subsequent gameplay input does not move the player or create bullets
- **AND** the cursor is no longer captured
