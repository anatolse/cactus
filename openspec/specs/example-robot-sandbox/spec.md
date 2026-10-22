# example-robot-sandbox Specification

## Purpose
Define a complete Cactus game skeleton in which a robot explores, excavates and constructs a procedural block world in first-person and strategy views.

## Requirements
### Requirement: Seeded bounded world supports three-dimensional edits
The example SHALL generate a 64 x 48 x 64 world of one-metre cells with x/z coordinates 0..63 and y coordinates -16..31, using std.noise Perlin for terrain/cavities and Voronoi for material regions. It SHALL provide solid protected bedrock, playable bounds and a guaranteed safe robot spawn. The same seed and generator version SHALL produce the same initial cells independent of generation batching. Generation SHALL display progress and finish before play begins.

#### Scenario: Repeat a seed
- **WHEN** two new worlds use the same seed and version with different generation batch sizes
- **THEN** their cell occupancy, materials and spawn location match

#### Scenario: Build above and dig below ground
- **WHEN** the player excavates below the original surface and builds above it
- **THEN** both edits exist in the same 3D world, including possible tunnels and overhangs

### Requirement: Robot explores the actual constructed landscape
The robot SHALL use standard character physics for collision, gravity, stepping and jumping. Connected passages with adequate clearance and reachable constructed surfaces SHALL be traversable in first person. A safe return-to-spawn action SHALL recover an entrapped robot. Recovery SHALL restore the 3 x 3 stone floor of the spawn pocket and the 3 x 3 x 3 air above it, and SHALL NOT change any other cell. The example SHALL use an existing robot model or an equivalent clearly visible robot representation in top view.

#### Scenario: Enter a constructed room
- **WHEN** the player builds a room with a two-cell-high entrance and walks through it
- **THEN** the robot enters the room, stands on its floor, and cannot pass through its walls

#### Scenario: Recover leaves cells outside the pocket alone
- **WHEN** the player builds on the layer directly above the spawn pocket, digs the layer directly under its floor, blocks the pocket, and recovers
- **THEN** the pocket floor is solid and the three pocket layers are air
- **AND** the cells above the pocket and under its floor keep the player's edits

### Requirement: Camera switching preserves the world and player
The player SHALL switch between captured-mouse first-person controls and an elevated strategy view with pointer, pan, zoom and centre-on-robot. Camera switching SHALL preserve robot position and world edits. Strategy view SHALL provide layer selection and a cutaway for underground work; first person SHALL show full geometry.

#### Scenario: Return from underground planning
- **WHEN** a player edits an underground layer in top view and switches back
- **THEN** the robot retains its current simulated position without a camera-induced teleport and the full world includes those edits

### Requirement: Single and bulk edits follow visible selection
First person SHALL dig a struck solid cell or build in its adjacent air cell within six metres. Top view SHALL preview and commit an inclusive rectangular cell volume using X/Z drag and Y-layer/range controls. The bottom palette SHALL contain labelled, distinct wood, stone and iron textures. Materials SHALL be unlimited. Build SHALL fill air only; dig SHALL remove eligible solids. Both SHALL respect bounds and bedrock; build SHALL skip cells intersecting the robot. The UI SHALL report skipped cells. UI-owned input SHALL not edit terrain.

#### Scenario: Bulk fill
- **WHEN** a 4 x 2 x 3 air-cell selection is committed as wood
- **THEN** its 24 eligible cells become solid wood in one edit operation

#### Scenario: Protected or occupied cell
- **WHEN** a build selection includes existing solids, bedrock or a cell intersecting the robot
- **THEN** those cells remain unchanged and the preview/result identify skipped cells

#### Scenario: Cancel or click the palette
- **WHEN** a selection is cancelled or a material button consumes a click
- **THEN** no terrain edit is committed from that action

### Requirement: Start and pause menus are functional
Startup SHALL display New Game, Load, Options and Exit. The in-game menu SHALL display Continue, Save, Load, Options and Exit. Options SHALL change look sensitivity, invert-Y and strategy camera pan speed and return to the originating menu. Menus and storage waits SHALL freeze robot physics, edits and game time. Menus SHALL remain interactive; storage SHALL show a busy state before work begins and report its outcome afterward, without promising nonblocking file I/O. Exit SHALL request application shutdown, with save/discard/cancel for an unsaved session and save-and-exit waiting for a successful save. While no world view exists, menus SHALL draw over their own backdrop colour, which the calendar's sky tint SHALL NOT change.

#### Scenario: Pause during a fall
- **WHEN** the menu opens while the robot is falling
- **THEN** its pose and falling velocity remain unchanged until Continue
- **AND** it resumes falling without a menu-time jump

#### Scenario: Save-and-exit fails
- **WHEN** saving before exit fails
- **THEN** the game remains open and displays the error instead of silently exiting

#### Scenario: Start menu backdrop
- **WHEN** the start menu has been shown for several frames
- **THEN** its viewport clears to the menu backdrop colour, not a sky colour

### Requirement: Saves restore edits and playable session state
The example SHALL use std.persistence with a documented single save slot. It SHALL preserve actual cell occupancy/materials, world seed/version, robot pose and velocity, camera mode/orientation, calendar, selected material and options. Restore SHALL rebuild ephemeral cameras, HUD and sky once, clear unfinished selections, and resume from the saved state. Missing, invalid or incompatible saves SHALL report an error while preserving the current world. Busy storage operations SHALL reject duplicate UI requests, and a completed load SHALL end the busy state so later requests are accepted.

#### Scenario: Excavation and construction survive load
- **WHEN** the player saves a tunnel and an iron tower, changes both, and loads
- **THEN** both return to the saved shapes/materials with correct picking and collision
- **AND** the robot, clock and selected view return to their saved state

#### Scenario: Load from the start menu
- **WHEN** a valid saved session is loaded before any new world is generated
- **THEN** a playable world, one HUD and the correct active camera appear without regenerating terrain

#### Scenario: Menus work after a load
- **WHEN** a load completes and the player pauses and saves again
- **THEN** the session is not busy and the save is carried out

### Requirement: Calendar speed has an explicit real-time ratio
The HUD SHALL show day, HH:MM and speed with decrease/increase buttons. Default 1x SHALL advance one game minute per real second while playing. Speeds SHALL be 0x, 0.5x, 1x, 2x, 4x and 8x with clamped endpoints. Time SHALL wrap every 1440 minutes and correctly count all crossed days. Calendar speed SHALL not alter movement/physics speed. Menus SHALL suspend the calendar.

#### Scenario: Normal day length
- **WHEN** 1440 real seconds of active play elapse at 1x
- **THEN** exactly one game day elapses, subject only to documented floating tolerance

#### Scenario: Accelerated midnight
- **WHEN** the clock is 23:59 at 2x and one real second of play elapses
- **THEN** the next day shows 00:01

#### Scenario: Freeze the sky
- **WHEN** speed is 0x
- **THEN** the clock and celestial motion stop while the robot remains controllable

### Requirement: Sun and moon follow the game calendar
The sun SHALL rise in the east at 06:00, cross the sky and set in the west at 18:00. The moon SHALL traverse the night sky in the opposite half of the same 24-hour cycle. Both SHALL circle the horizontal centre of the current world, whatever its size. Visible celestial bodies and directional lighting SHALL use the same calendar, with continuous horizon transitions. Caves SHALL remain usable through a robot lamp.

#### Scenario: Day and night observations
- **WHEN** the clock is advanced through sunrise, noon, sunset and midnight
- **THEN** sun/moon positions and lighting agree with their specified half-cycles
- **AND** changing speed changes their rate without a discontinuous position jump

#### Scenario: Sky over a small world
- **WHEN** the 16 x 12 x 16 test world is playing at any time of day
- **THEN** the sun and moon discs sit at equal distances on opposite sides of that world's horizontal centre

### Requirement: The example demonstrates authored gameplay and verified rendering
Generation, edits, menus, camera control, calendar and save policy SHALL be Cactus logic using stdlib features. Noise algorithms and exact ray/box math SHALL also be Cactus stdlib funcs. Native additions SHALL be limited to reusable engine integration and measured generic optimization. The example SHALL include headless behavior coverage, real-GL visual evidence for textures/sky/cutaway, and recorded full-world performance/save measurements. At 1280x720 on the documented development machine it SHALL target steady 60 FPS and a 16 x 8 x 16 edit visible within 100 ms; results and hardware SHALL be reported rather than inferred from headless success.

#### Scenario: Material appearance is verified
- **WHEN** the visual acceptance run displays wood, stone and iron side by side
- **THEN** screenshots show different actual texture patterns rather than only different solid tints
