## Requirements

### Requirement: std.render.sprites provides 2D rendering traits
The `std.render.sprites` module SHALL provide traits for 2D sprite/texture rendering. These are "passive traits" — the Raylib backend SHALL automatically render entities with these traits in combination with a `std.transform.flat.Transform` or `std.transform.volume.Transform`. No user-written render system is needed for standard rendering behavior.

#### Scenario: Renderer trait fields
- **WHEN** `use std.render.sprites as spr` is imported and a unit applies `spr.Renderer`
- **THEN** the entity has fields: `texture: texture_id` (let), `size: vec2`, `color: color`, `visible: bool`, `layer: int`

#### Scenario: 2D entity is rendered automatically
- **WHEN** an entity has both a Transform trait and `spr.Renderer` applied
- **THEN** the backend draws the sprite at the transform's position each frame without any user-written system

#### Scenario: Invisible sprite skips rendering
- **WHEN** `Renderer.visible = false` on an entity
- **THEN** the backend does not draw the sprite that frame

#### Scenario: AnimatedSprite cycles through frames
- **WHEN** an entity has `spr.AnimatedSprite` with `playing = true` and `frame_count > 1`
- **THEN** the backend advances `frame` at `fps` frames per second, wrapping at `frame_count`

---

### Requirement: std.render.meshes provides 3D rendering traits
The `std.render.meshes` module SHALL provide traits for 3D mesh and billboard rendering, plus light source traits. These are passive traits read by the 3D Raylib backend.

#### Scenario: Renderer trait fields for meshes
- **WHEN** `use std.render.meshes as mesh` is imported and a unit applies `mesh.Renderer`
- **THEN** the entity has fields: `mesh: mesh_id` (let), `material: material_id` (let), `visible: bool`, `cast_shadow: bool`

#### Scenario: 3D entity is rendered automatically
- **WHEN** an entity has `std.transform.volume.Transform` and `mesh.Renderer` applied
- **THEN** the backend draws the mesh at the transform's position with the transform's rotation and scale

#### Scenario: BillboardRenderer faces the camera
- **WHEN** an entity has a volume Transform and `mesh.BillboardRenderer`
- **THEN** the backend draws the billboard texture always facing the active camera

#### Scenario: PointLight contributes to scene lighting
- **WHEN** an entity has a volume Transform and `mesh.PointLight` with `enabled = true`
- **THEN** the backend uses it as a scene point light source at the transform's position

---

### Requirement: Render traits are orthogonal to transform sub-modules
An entity MAY use any combination of render and transform sub-modules. `std.render.sprites.Renderer` may be combined with either `std.transform.flat.Transform` (2D sprite) or `std.transform.volume.Transform` (3D billboard sprite). The backend handles both combinations.

#### Scenario: Sprites in 3D world
- **WHEN** an entity has `std.transform.volume.Transform` and `std.render.sprites.Renderer`
- **THEN** the backend renders the sprite as a billboard in the 3D world at the 3D position

#### Scenario: Meshes require volume Transform
- **WHEN** an entity has `std.render.meshes.Renderer` but no Transform applied
- **THEN** the backend renders the mesh at world origin (no transform = no position)
