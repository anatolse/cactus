# stdlib-voxel-volume Specification

## Purpose
Describe editable block volumes using ordinary Cactus entities while providing exact picking and efficient textured surface rendering.

## Requirements
### Requirement: Block state uses ECS traits and ordinary persistence
std.voxel.volume SHALL describe a fixed, axis-aligned volume and cells identified by a volume reference and integer coordinates. The volume's origin, cell size and bounds SHALL be `let` fields, given only when the volume is made. Each cell coordinate SHALL be a ranged `let` field whose range is the volume's bounds on that axis. Its reusable cell template SHALL compose existing world transforms and box colliders. std.physics.volume.Solid membership SHALL determine occupancy. A persistent texture_id field SHALL select the surface. Air cells SHALL remain addressable and save-eligible. The volume configuration SHALL also be persistent so a restored cell retains a valid remapped volume reference. Editing SHALL use ordinary field updates and trait addition/removal, without opaque terrain mutation calls.

#### Scenario: Dig and rebuild the same cell
- **WHEN** Solid is removed from a cell and subsequently added with a selected texture
- **THEN** that same cell identity changes from occupied to air to occupied
- **AND** collision and visible surfaces follow the committed occupancy

#### Scenario: Save an excavated cell
- **WHEN** a save containing a cell without Solid is restored
- **THEN** the cell remains air and is not filled by terrain regeneration

#### Scenario: Volume geometry cannot change
- **WHEN** a handler writes a volume's `max_x` or adds `Volume` to an existing entity
- **THEN** compilation fails with a diagnostic naming the field or the trait

### Requirement: Cell and ray probes are ordinary Cactus rules
The module SHALL expose CellProbe and RayProbe traits whose results are computed by ordinary Cactus pair/reduction rules during the input phase. Producers SHALL run before probe evaluation and consumers afterward through normal handler dependencies. Probe results SHALL describe that pass's selected state, and SHALL be refreshed on each input activation. Disabled probes, invalid volumes and empty candidate groups SHALL clear old results.

CellProbe SHALL select a matching volume/coordinate cell, including air, or a stale handle when none exists. RayProbe SHALL consider solid cells of the selected volume, call the pure std.collision.volume box test and select the nearest hit using existing reduction semantics. Its result SHALL expose hit status, cell identity, coordinates, world distance, point, face normal and adjacent coordinate. Invalid rays and nonpositive limits SHALL produce a miss. Face ties within a box SHALL use X, Y, Z order; equal hit distances across cells SHALL use stable creation order. An inside-solid hit SHALL have distance zero and zero normal, with no implied adjacent placement.

#### Scenario: Place against a struck face
- **WHEN** the ray probe hits a cell's positive-X face and the consumer runs after probe evaluation
- **THEN** the result normal is positive X and the adjacent coordinate is one cell farther along X

#### Scenario: Ray passes close to a cell centre
- **WHEN** a ray passes near a cell centre but outside its cube
- **THEN** that cell is not selected as a hit

#### Scenario: Miss replaces a previous hit
- **WHEN** a probe previously hit a solid but its current ray misses all eligible cells
- **THEN** its next evaluated result is an explicit miss without a stale selected target

#### Scenario: Trait removal respects commit
- **WHEN** an edit removes Solid after the current input probe pass
- **THEN** that pass's result remains its sampled result
- **AND** the next input probe pass observes the committed removal

#### Scenario: Portable reference behavior
- **WHEN** the same world is evaluated by the Cactus probe rules with and without backend acceleration
- **THEN** the selected cell, hit result and observable ordering agree

### Requirement: Volume geometry has defined validity
A volume SHALL require finite origin, positive finite cell size and nonempty integer bounds whose extent is representable. Invalid configurations SHALL produce a diagnostic and no voxel query/render result. A cell whose coordinate lies outside its volume's bounds SHALL never be created; the attempt SHALL be reported as a range failure. Duplicate coordinates SHALL be diagnosed. CellProbe SHALL choose the earliest-created live matching cell; ray probes and rendering SHALL use occupied candidates with stable creation-order ties. The example SHALL construct unique, correctly positioned cells, so normal play never depends on duplicate-coordinate behavior.

#### Scenario: Duplicate coordinate diagnosis
- **WHEN** two live cells refer to the same volume and coordinate
- **THEN** the volume reports the duplicate and CellProbe consistently selects the earliest-created cell

#### Scenario: A cell outside its volume is not created
- **WHEN** a handler spawns a cell template with `x` one past its volume's `max_x`
- **THEN** no cell entity is created and the runtime reports the range failure

### Requirement: Surfaces share lighting and preserve per-cell textures
A volume renderer SHALL draw exposed occupied-cell faces with the selected texture, repeating once per cell. Hidden internal faces SHALL be omitted. Surface batching SHALL preserve visible geometry, texture orientation and existing point/directional lighting. Authors SHALL NOT manage chunks, GPU handles or dirty flags.

#### Scenario: Dig exposes a cave wall
- **WHEN** a solid cell between air and another solid cell is removed at commit
- **THEN** the next render shows the newly exposed neighbour face with its texture

#### Scenario: Adjacent materials remain distinct
- **WHEN** wood, stone and iron cells share faces and lie on a common visible plane
- **THEN** each visible cell retains its own texture and cell-scale repetition

### Requirement: Cutaway is view-local presentation
A camera's voxel cutaway SHALL omit cells above its selected layer and expose the resulting section surfaces. It SHALL NOT remove solids, change full-world queries, modify collision or alter persistence. Other cameras without that cutaway SHALL see the complete world.

#### Scenario: Inspect underground construction
- **WHEN** the strategy camera selects a layer below the terrain surface
- **THEN** that camera shows the section at the layer
- **AND** switching to first person shows the full terrain with unchanged collision

### Requirement: Derived services respect commits and world replacement
Rendering SHALL observe direct texture updates and committed structural changes under ordinary Cactus timing. Probe results SHALL follow their declared input pass timing. Restore, cell destruction and volume replacement SHALL invalidate stale derived data. An unchanged frame SHALL not rebuild all geometry; an isolated edit SHALL not require remeshing the whole world.

#### Scenario: Restore after excavation
- **WHEN** a saved solid cell replaces the current excavated cell during restore
- **THEN** subsequent picking and rendering reflect the restored solid with no old cached surface or entity handle

### Requirement: Volume renderers choose a surface style
`std.voxel.volume` SHALL declare `pub enum SurfaceStyle` with `Blocky` and `Smooth`, and `VolumeRenderer` SHALL gain `var style: SurfaceStyle = SurfaceStyle.Blocky`, `var roughness: float = 0.25` and `var seed: int = 0`. `Blocky` SHALL draw exactly what volume renderers draw today. `roughness` is a fraction of `cell_size`. Values below `0.0` SHALL act as `0.0`, and values above `0.4` SHALL act as `0.4`. Changing `style`, `roughness` or `seed` SHALL rebuild the volume's surface by the next render.

#### Scenario: Default stays blocky
- **WHEN** a program declares a `VolumeRenderer` without setting `style`
- **THEN** the volume renders with the same faces as before this change

#### Scenario: Switching style
- **WHEN** a handler sets `style = SurfaceStyle.Smooth` on a rendered volume
- **THEN** the next render draws the smooth surface

### Requirement: The smooth surface is rounded, closed and bounded
With `Smooth`, a volume renderer SHALL draw one surface that separates solid cells from air and from the volume's outside. That surface SHALL be closed wherever solid cells meet air, with no gaps at chunk borders. Every surface point SHALL lie within `(0.5 + roughness) × cell_size` of the center of some solid cell on every axis, that is, inside that cell's box grown by `roughness × cell_size`. A single solid cell SHALL produce a visible rounded shape, not nothing. With `roughness = 0.0`, the surface SHALL depend only on which cells are solid.

#### Scenario: A dug tunnel
- **WHEN** a straight line of cells inside solid rock is made air
- **THEN** the render shows a rounded tunnel whose walls stay within the bound around the neighboring solid cells

#### Scenario: Chunk seams
- **WHEN** a solid region spans two surface chunks
- **THEN** the surface has no gap or crack along the chunk border

#### Scenario: Lone cell
- **WHEN** one solid cell is surrounded by air
- **THEN** a rounded shape is drawn within that cell's bound

### Requirement: Smooth surfaces are deterministic
The smooth surface SHALL be a function of the volume description, the set of solid cells, their textures, `roughness` and `seed` only. It SHALL NOT depend on frame timing, edit order, chunk rebuild order or entity creation order. Two runs with the same world, including a world restored from a save, SHALL draw the same vertices. A different `seed` MAY change vertex positions but SHALL NOT change which cells the surface encloses.

#### Scenario: Same world after restore
- **WHEN** a world with a smooth volume is saved and restored
- **THEN** the restored render's vertices equal the saved world's vertices

#### Scenario: Edit order does not matter
- **WHEN** two runs dig the same five cells in different orders and in different frames
- **THEN** both runs draw the same surface after the last dig

### Requirement: Smooth surfaces are visual only
Choosing `Smooth` SHALL NOT change collision, character bodies, cell or ray probes, `Solid`, persistence, or which cells a `Cutaway` hides. A cutaway SHALL hide the smooth surface above its layer and SHALL show the cut as for `Blocky`.

#### Scenario: Collision unchanged
- **WHEN** a character body walks into a wall drawn with `Smooth`
- **THEN** it stops at the cell box, exactly as with `Blocky`

#### Scenario: Cutaway on a smooth volume
- **WHEN** a camera's `Cutaway` selects a layer of a smooth volume
- **THEN** that camera hides the surface above the layer and shows the section at the layer

### Requirement: Smooth surfaces keep per-cell textures and incremental updates
Each point of a smooth surface SHALL take the texture of the nearest solid cell, with ties broken by the lowest cell coordinate in `x`, then `y`, then `z`. The texture SHALL be projected so it keeps about one repeat per cell on walls, floors and slopes. A committed change to one cell's `Solid` or texture SHALL rebuild only the chunks whose surface that cell can change, which are the chunks that hold the cell or one of its 26 neighbors.

#### Scenario: Two materials on one wall
- **WHEN** a stone cell and a gold cell form one smooth wall
- **THEN** each part of the wall shows its nearest cell's texture

#### Scenario: An isolated dig
- **WHEN** one cell in the middle of a large smooth volume is dug
- **THEN** only the chunks within one cell of it are rebuilt
