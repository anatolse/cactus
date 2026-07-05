# example-split-screen-forest-bombs Specification

## Purpose

Define the split-screen forest bombs cpp-entt example and the gameplay/presentation behavior it demonstrates.

## Requirements

### Requirement: Split-screen forest bombs example source exists
The repository SHALL provide `examples/split-screen-forest-bombs/forest_bombs.cactus` as a standalone Cactus example entrypoint for the cpp-entt backend.

#### Scenario: Example source is present
- **WHEN** the repository examples are inspected
- **THEN** `examples/split-screen-forest-bombs/forest_bombs.cactus` exists as the root source for the split-screen forest bombs example

#### Scenario: Example imports required stdlib surfaces
- **WHEN** `forest_bombs.cactus` is read
- **THEN** it imports the stdlib modules needed for 3D transforms, 3D mesh rendering, 3D cameras, viewports, input, and 3D physics volume queries

### Requirement: Example demonstrates split-screen 3D player presentation
The split-screen forest bombs example SHALL declare two local player-controlled gameplay entities and two 3D camera viewport entities that render the scene in left and right screen halves.

#### Scenario: Two viewport cameras are declared
- **WHEN** the example scene entities are analyzed
- **THEN** it contains one camera viewport covering the left half of the screen and one camera viewport covering the right half of the screen

#### Scenario: Two player entities are declared
- **WHEN** the example scene entities are analyzed
- **THEN** it contains two player gameplay entities with movement input state, bomb inventory state, and tree-hit counter state

### Requirement: Example demonstrates randomly varied growing blocky trees
The split-screen forest bombs example SHALL model trees as gameplay-destructible tree roots with randomized placement/growth data and simple brown/green box visuals.

#### Scenario: Tree gameplay root carries growth state
- **WHEN** a tree instance is created by the example
- **THEN** its logical root has tree identity plus growth progress, scale factor, and growth slowdown data

#### Scenario: Tree visual uses two boxes
- **WHEN** a tree instance is presented by the example
- **THEN** it is represented by a brown trunk box and a green crown box associated with the same logical tree

#### Scenario: Forest instances are varied
- **WHEN** the example initializes its forest
- **THEN** tree instances use varied X/Z positions, scale factors, and growth slowdown values within bounded ranges

### Requirement: Example demonstrates bomb explosions through 3D volume queries
The split-screen forest bombs example SHALL demonstrate orange thrown bombs that explode and destroy nearby tree roots using `std.physics.volume.query.overlap_sphere[...]`.

#### Scenario: Bomb uses authored projectile motion
- **WHEN** a player throws a bomb in the example
- **THEN** the example spawns or activates an orange bomb entity with velocity/gravity state that advances through ordinary gameplay systems

#### Scenario: Explosion queries nearby trees
- **WHEN** a bomb impact creates an explosion
- **THEN** the example uses a 3D overlap-sphere query filtered to logical tree roots to find trees in the explosion radius

#### Scenario: Explosion destroys whole trees and updates score
- **WHEN** the explosion query returns tree roots owned by live tree instances
- **THEN** the example destroys the affected logical trees and updates the throwing player's tree-hit counter

### Requirement: Example remains intentionally minimal UI
The split-screen forest bombs example SHALL avoid requiring minimap, arrow indicator, render-to-texture, or a dedicated UI DSL.

#### Scenario: Example has no fancy UI dependency
- **WHEN** the example is compiled for cpp-entt
- **THEN** successful compilation does not depend on minimap rendering, player-arrow overlay rendering, render-to-texture support, or a first-class UI DSL