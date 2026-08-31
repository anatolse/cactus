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
The example SHALL author a roofless square arena in Cactus entities and templates. It SHALL include a dark-grey floor, light-grey perimeter walls, a brown central cubic building with two opposite ground-level door openings, an exterior staircase, a walkable building roof, and at least one low solid obstacle short enough for an enemy to vault. The active viewport SHALL provide a blue sky background.

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

#### Scenario: A low obstacle exists for enemies to vault
- **WHEN** the authored map entities are inspected
- **THEN** at least one solid obstacle shorter than the perimeter walls and building walls lies in a path an enemy takes toward the player
- **AND** its height is short enough that the vaulting behavior in "Live enemies seek the player and end the game on contact" applies to it

### Requirement: Arena is lit by a directional sun light
The example SHALL author one enabled directional light representing sunlight, angled so it illuminates the floor, walls, and building rather than leaving authored map geometry and character models rendering unlit.

#### Scenario: Sun light is present and enabled
- **WHEN** the authored entities are inspected
- **THEN** exactly one entity carries an enabled `std.render.meshes.DirectionalLight`

#### Scenario: Lit surfaces are visibly brighter than an unlit render
- **WHEN** one frame is rendered with the authored sun light enabled
- **THEN** floor, wall, and building surfaces facing toward the light render visibly brighter than they would with the light disabled

### Requirement: Player uses first-person controls and a collider
The player SHALL move with W/A/S/D input relative to its horizontal facing direction and look with mouse delta using yaw and pitch. Pitch SHALL be clamped to avoid flipping. The first-person camera SHALL remain attached to the player body, and the player body SHALL carry volume Collider and CapsuleCollider traits used by authored arena collision and grounding rules. The mouse cursor SHALL be captured when play begins; pressing Escape SHALL release cursor capture without ending the game, and a subsequent primary click while released SHALL recapture it. Cursor capture SHALL also be released automatically on game over and recaptured automatically on restart. The player SHALL fall under gravity when unsupported and SHALL be able to jump with a dedicated input while grounded; falling and jumping SHALL be gradual (velocity-driven over time) rather than an instantaneous position change, while climbing a small step (such as the exterior staircase) SHALL remain an immediate snap as before.

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
- **THEN** authored grounding raises the player by each reachable step immediately
- **AND** the player can stand and move on the building roof

#### Scenario: Escape releases cursor capture without ending play
- **WHEN** the cursor is captured and Escape is pressed while the game is active (not game over)
- **THEN** the cursor is released
- **AND** the game remains active, not in the game-over state

#### Scenario: Primary click recaptures a released cursor
- **WHEN** the cursor is released (not via game over) and a primary click occurs
- **THEN** the cursor is recaptured
- **AND** that same click does not also fire a bullet

#### Scenario: Walking off a ledge falls gradually instead of teleporting
- **WHEN** the player moves past the edge of an elevated surface such as the building roof, with no lower step within stepping range
- **THEN** the player's height decreases progressively over multiple ticks under gravity rather than snapping instantly to the surface below
- **AND** the player comes to rest on the lower surface once reached

#### Scenario: Jumping while grounded launches the player upward
- **WHEN** the jump input is pressed while the player is grounded
- **THEN** the player's height increases over subsequent ticks under an upward impulse before gravity returns it to the ground
- **AND** the player's horizontal movement is unaffected

#### Scenario: Jump input is ignored while airborne
- **WHEN** the jump input is pressed while the player is already falling or mid-jump
- **THEN** no additional upward impulse is applied

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
Robot and knight enemies SHALL render their reused models with animation, carry volume Collider and CapsuleCollider traits, face and move toward the live player, and use authored solid-box separation to remain inside the arena and respond to the building. Each enemy's model-animator playback speed SHALL be derived from its current movement speed so its running clip's stride visually matches its translation. When a wall directly blocks an enemy's straight-line path to the player, the enemy SHALL steer toward an alternate unobstructed heading rather than remaining stuck against the wall. When an enemy's chosen heading is blocked by an obstacle short enough to clear, the enemy SHALL jump over it using the same gravity-driven vertical motion available to the player, switching to a jump animation clip where its model has one. A live enemy reaching the player's collider SHALL trigger game over.

#### Scenario: Enemy advances toward player
- **WHEN** a live enemy and player are separated with no solid box between them
- **THEN** successive fixed updates reduce their horizontal distance

#### Scenario: Enemy responds to a solid box
- **WHEN** an enemy's attempted movement overlaps a wall or building box
- **THEN** authored separation prevents penetration while preserving any unblocked tangential movement

#### Scenario: Enemy contact triggers game over
- **WHEN** a live enemy overlaps the player's collider
- **THEN** the player enters game over exactly once

#### Scenario: Enemy animation speed matches movement speed
- **WHEN** a live enemy is moving toward the player
- **THEN** its model-animator playback speed is derived from its current movement speed rather than a fixed independent value

#### Scenario: Enemy steers around a wall blocking the direct path
- **WHEN** a wall lies directly between a live enemy and the player, blocking the enemy's straight-line heading
- **THEN** the enemy moves along an alternate heading that is not blocked
- **AND** the enemy's position changes tick over tick rather than remaining stuck oscillating against the wall

#### Scenario: Enemy vaults a short obstacle in its path
- **WHEN** a live enemy's chosen heading is blocked by a solid obstacle short enough to clear
- **THEN** the enemy gains upward velocity and passes over the obstacle instead of being blocked by it
- **AND** the enemy plays a jump animation clip if its model has one, or otherwise continues playing its existing movement clip through the vault

#### Scenario: Enemy does not attempt to vault a wall taller than it can clear
- **WHEN** a live enemy's chosen heading is blocked by a perimeter wall or building wall
- **THEN** the enemy does not jump
- **AND** authored separation and steering handle the obstacle as before

### Requirement: Live enemies do not overlap each other
While multiple live (non-dying) enemies are simultaneously present, authored separation SHALL keep their capsule colliders from penetrating one another, regardless of enemy kind. Enemies in their death transition SHALL NOT participate in this separation.

#### Scenario: Two robots are pushed apart
- **WHEN** two live robot enemies' colliders would overlap
- **THEN** authored separation moves them apart so their colliders no longer overlap

#### Scenario: A robot and a knight are pushed apart
- **WHEN** a live robot enemy's collider would overlap a live knight enemy's collider
- **THEN** authored separation moves them apart so their colliders no longer overlap

#### Scenario: Two knights are pushed apart
- **WHEN** two live knight enemies' colliders would overlap
- **THEN** authored separation moves them apart so their colliders no longer overlap

#### Scenario: A dying enemy does not participate in separation
- **WHEN** one of two overlapping enemies has begun its death transition
- **THEN** authored separation does not move either enemy for that pair

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

### Requirement: Player can restart the game after game over
While in the game-over state, pressing "R" SHALL restart the game: all live enemies and in-flight bullets SHALL be removed, the player SHALL return to its original spawn position and orientation with `game_over` cleared, spawn points SHALL resume producing enemies on the same immediate-first-wave schedule as a fresh game start, and the crosshair/game-over HUD and cursor capture SHALL return to their start-of-game state. Restarting SHALL NOT reset the player's accumulated game-over count.

#### Scenario: Restart clears live enemies and bullets
- **WHEN** "R" is pressed while the game is over
- **THEN** no `Enemy` or `Bullet` entities remain immediately after the restart completes

#### Scenario: Restart returns the player to a playable state
- **WHEN** "R" is pressed while the game is over
- **THEN** `game_over` becomes false
- **AND** the player is positioned and oriented at its original spawn transform
- **AND** the cursor is captured again

#### Scenario: Restart resumes enemy spawning
- **WHEN** "R" is pressed while the game is over and gameplay continues afterward
- **THEN** each spawn point produces its first post-restart enemy on the same immediate-first-wave schedule as the original game start

#### Scenario: Restart restores the crosshair and hides the game-over label
- **WHEN** "R" is pressed while the game is over
- **THEN** the crosshair becomes visible again
- **AND** the GAME OVER label becomes hidden

#### Scenario: Restart does not reset the death counter
- **WHEN** "R" is pressed after at least one prior game over
- **THEN** the player's accumulated game-over count is unchanged by the restart

#### Scenario: "R" has no effect during active play
- **WHEN** "R" is pressed while the game is not in the game-over state
- **THEN** no restart occurs and ongoing gameplay is unaffected
