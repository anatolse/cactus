# example-forest-bombs-player-overhaul Specification

## Purpose

Define the player-overhaul behavior for the split-screen forest bombs example: capsule mesh players, yaw-rotation controls, facing-direction movement, fly controls, facing-direction bomb throws, and first-person camera positioning.

## Requirements

### Requirement: Players use capsule mesh and capsules are rendered as capsules
The split-screen forest bombs example SHALL render each player as a capsule mesh (not a box) so the player silhouette reads as a character shape.

#### Scenario: Player mesh asset is capsule
- **WHEN** `forest_bombs.cactus` is read
- **THEN** a `CapsuleMesh` asset is declared with path `"models/capsule.mesh"` and both player entities reference it in their `Renderer` component

### Requirement: Bombs use a low-polygon sphere mesh
The split-screen forest bombs example SHALL render each thrown bomb as a low-polygon sphere mesh rather than a box.

#### Scenario: Bomb mesh asset is a low-poly sphere
- **WHEN** `forest_bombs.cactus` is read
- **THEN** a `SphereMesh` asset is declared with path `"models/sphere_lowpoly.mesh"` and the `BombTemplate` and `ThrowBomb` spawn reference it

### Requirement: Left/right input rotates the player around the world Y axis
The split-screen forest bombs example SHALL interpret the left/right input axis as a yaw rotation delta applied to the player entity's `WorldTransform.rotation` each tick, not a lateral position offset.

#### Scenario: A/D input yaws Player 1
- **WHEN** Player 1's A key is held
- **THEN** the `MovePlayer` rule applies a negative yaw delta to `Player1`'s and `Player1Camera`'s `WorldTransform.rotation` each tick at a rate governed by `TURN_SPEED`

#### Scenario: Arrow left/right yaws Player 2
- **WHEN** Player 2's Right arrow key is held
- **THEN** the `MovePlayer` rule applies a positive yaw delta to `Player2`'s and `Player2Camera`'s `WorldTransform.rotation` each tick

### Requirement: Forward/backward input moves the player in their facing direction
The split-screen forest bombs example SHALL move the player along the XZ projection of their current facing direction (`quat.forward(rotation)`) when forward/backward input is applied, not along the world Z axis.

#### Scenario: W/S moves Player 1 in their facing direction
- **WHEN** Player 1's W key is held and the player is rotated 90° around Y
- **THEN** the `MovePlayer` rule moves the player along their facing direction (world +X), not world -Z

#### Scenario: Up/Down arrows move Player 2 in their facing direction
- **WHEN** Player 2's Up arrow key is held
- **THEN** the `MovePlayer` rule advances Player 2's position in the XZ projection of their current facing direction

### Requirement: Fly-up and fly-down inputs move the player vertically
The split-screen forest bombs example SHALL support vertical (world Y) movement driven by dedicated fly-up and fly-down input axes. Player 1 uses R (up) / F (down); Player 2 uses PageUp (up) / PageDown (down). The `PlayerInput` trait SHALL carry a `fly: float` field.

#### Scenario: P1FlyAxis input actions are declared
- **WHEN** `forest_bombs.cactus` is read
- **THEN** it declares `P1FlyAxis` as an `axis` with `negative = Key.R` and `positive = Key.F`

#### Scenario: P2FlyAxis input actions are declared
- **WHEN** `forest_bombs.cactus` is read
- **THEN** it declares `P2FlyAxis` as an `axis` with `negative = Key.PageUp` and `positive = Key.PageDown`

#### Scenario: Fly input moves player vertically
- **WHEN** a player's `fly` field is positive (fly-up held)
- **THEN** the `MovePlayer` rule adds `fly * FLY_SPEED * tick.dt` to `position.y` for both the player body and camera entity

### Requirement: Bombs are thrown toward the center of the throwing player's screen
The split-screen forest bombs example SHALL compute the bomb's initial XZ velocity from `quat.forward(rotation)` evaluated on the throwing player's current `WorldTransform.rotation`, so bombs always fly in the direction the player faces.

#### Scenario: Bomb velocity follows player facing
- **WHEN** a player with a 90° yaw rotation throws a bomb
- **THEN** the spawned bomb's initial velocity vector has its dominant horizontal component along the player's current facing direction (world +X for 90° yaw), not a hard-coded world -Z

### Requirement: Camera is positioned at the player's eye in first-person
The split-screen forest bombs example SHALL place each player's camera entity directly above the player body at `EYE_HEIGHT` with no back-offset (`CAMERA_BACK` constant removed), and the camera's initial rotation SHALL be `quat.identity()` so yaw deltas from the shared `MovePlayer` rule accumulate correctly.

#### Scenario: Camera initial position is at eye height, no back-offset
- **WHEN** the example scene is initialized
- **THEN** `Player1Camera` position equals `Player1` position plus `(0, EYE_HEIGHT, 0)` with no Z offset
- **AND** `Player2Camera` position equals `Player2` position plus `(0, EYE_HEIGHT, 0)` with no Z offset

#### Scenario: Camera initial rotation is identity
- **WHEN** the example scene is initialized
- **THEN** both `Player1Camera` and `Player2Camera` have `WorldTransform.rotation = quat.identity()`
