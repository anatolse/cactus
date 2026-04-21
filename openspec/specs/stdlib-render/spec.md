## Requirements

### Requirement: std.render.sprites exposes the shipped sprite trait surface
The `std.render.sprites` module SHALL expose the passive trait surface currently declared in the shipped stdlib module.

#### Scenario: Renderer trait fields
- **WHEN** `use std.render.sprites as spr` is imported and a unit applies `spr.Renderer`
- **THEN** the entity has fields: `texture: texture_id` (let), `size: vec2`, `color: color`, `visible: bool`, `layer: int`

#### Scenario: AnimatedSprite trait fields
- **WHEN** `use std.render.sprites as spr` is imported and a unit applies `spr.AnimatedSprite`
- **THEN** the entity has fields: `texture: texture_id` (let), `frame: int`, `frame_count: int`, `fps: float`, `playing: bool`

#### Scenario: Canvas2D trait fields
- **WHEN** `use std.render.sprites as spr` is imported and a unit applies `spr.Canvas2D`
- **THEN** the entity has fields: `color: color`, `visible: bool`

---

### Requirement: std.render.sprites declares the current backend-backed extern systems
The shipped `std.render.sprites` module SHALL declare the currently implemented passive render extern systems for sprite submission and animated-sprite advancement.

#### Scenario: SpriteRenderer declaration matches shipped stdlib
- **WHEN** `std.render.sprites` is imported
- **THEN** the module declares `extern system SpriteRenderer` with filter entries `std.transform.flat.WorldTransform` and `Renderer`

#### Scenario: AnimatedSpriteSystem declaration matches shipped stdlib
- **WHEN** `std.render.sprites` is imported
- **THEN** the module declares `extern system AnimatedSpriteSystem` with filter entry `AnimatedSprite`

#### Scenario: Flat-world sprite rendering is backend-backed
- **WHEN** a program imports `std.render.sprites` and the recognized `SpriteRenderer` extern system is scheduled
- **THEN** generated output binds to backend-library sprite rendering behavior for entities with `std.transform.flat.WorldTransform` and `Renderer`
- **AND** that backend behavior resolves sprite texture handles through the asset/runtime infrastructure before drawing visible sprites in the backend-owned 2D render pass

#### Scenario: Invisible sprite skips drawing
- **WHEN** sprite submission is invoked with `Renderer.visible = false`
- **THEN** the backend does not draw that entity in the sprite pass

#### Scenario: Sprite layer controls draw order
- **WHEN** two visible sprites are drawn in the same frame with different `Renderer.layer` values on the cpp-entt-backed sprite path
- **THEN** the cpp-entt backend-owned sprite path draws the lower layer before the higher layer

#### Scenario: Animated sprite advancement follows current helper semantics
- **WHEN** the recognized `AnimatedSpriteSystem` extern system is scheduled for an `AnimatedSprite`
- **THEN** generated output binds to backend-library animation advancement behavior that first validates the texture asset handle
- **AND** the backend advances `frame` only when `playing = true`, `frame_count > 0`, `fps > 0`, and the computed integer animation step is greater than zero
- **AND** when advancement occurs, the backend wraps `frame` modulo `frame_count`

---

### Requirement: std.render.meshes exposes the shipped mesh, billboard, and light trait surface
The `std.render.meshes` module SHALL expose the passive 3D render and light traits currently declared in the shipped stdlib module.

#### Scenario: Mesh renderer trait fields
- **WHEN** `use std.render.meshes as mesh` is imported and a unit applies `mesh.Renderer`
- **THEN** the entity has fields: `mesh: mesh_id` (let), `material: material_id` (let), `visible: bool`, `cast_shadow: bool`

#### Scenario: Billboard renderer trait fields
- **WHEN** `use std.render.meshes as mesh` is imported and a unit applies `mesh.BillboardRenderer`
- **THEN** the entity has fields: `texture: texture_id` (let), `size: vec2`, `color: color`, `visible: bool`

#### Scenario: PointLight trait fields
- **WHEN** `use std.render.meshes as mesh` is imported and a unit applies `mesh.PointLight`
- **THEN** the entity has fields: `color: color`, `intensity: float`, `range: float`, `enabled: bool`

#### Scenario: DirectionalLight trait fields
- **WHEN** `use std.render.meshes as mesh` is imported and a unit applies `mesh.DirectionalLight`
- **THEN** the entity has fields: `direction: vec3`, `color: color`, `intensity: float`, `enabled: bool`

---

### Requirement: std.render.meshes declares the current stdlib-owned mesh and light extern systems
The shipped `std.render.meshes` module SHALL declare the currently implemented stdlib-owned extern systems for mesh rendering and light registration.

#### Scenario: MeshRenderer declaration matches shipped stdlib
- **WHEN** `std.render.meshes` is imported
- **THEN** the module declares `extern system MeshRenderer` with filter entries `std.transform.volume.WorldTransform` and `Renderer`

#### Scenario: PointLightSystem declaration matches shipped stdlib
- **WHEN** `std.render.meshes` is imported
- **THEN** the module declares `extern system PointLightSystem` with filter entries `std.transform.volume.WorldTransform` and `PointLight`

#### Scenario: DirectionalLightSystem declaration matches shipped stdlib
- **WHEN** `std.render.meshes` is imported
- **THEN** the module declares `extern system DirectionalLightSystem` with filter entry `DirectionalLight`

#### Scenario: BillboardRenderer trait is exposed without a stdlib-declared auto-included extern system
- **WHEN** `std.render.meshes` is imported
- **THEN** the module exposes `BillboardRenderer` as a trait
- **AND** the shipped stdlib module does not declare an `extern system BillboardRenderer`

#### Scenario: Mesh rendering is backend-backed
- **WHEN** a program imports `std.render.meshes` and the recognized `MeshRenderer` extern system is scheduled
- **THEN** generated output binds to backend-library 3D mesh rendering behavior for entities with `std.transform.volume.WorldTransform` and mesh `Renderer`
- **AND** mesh/material handles resolve through the asset/runtime infrastructure before drawing visible meshes in the cpp-entt backend-owned 3D render pass

#### Scenario: Invisible mesh skips drawing
- **WHEN** mesh rendering is invoked with `Renderer.visible = false`
- **THEN** the backend does not draw that entity in the mesh pass

#### Scenario: Point lights are backend-backed
- **WHEN** a program imports `std.render.meshes` and the recognized `PointLightSystem` extern system is scheduled
- **THEN** generated output binds to backend-library point-light registration behavior for entities with `std.transform.volume.WorldTransform` and `PointLight`
- **AND** enabled point lights contribute a registration event through the backend runtime adapter

#### Scenario: Directional lights are backend-backed
- **WHEN** a program imports `std.render.meshes` and the recognized `DirectionalLightSystem` extern system is scheduled
- **THEN** generated output binds to backend-library directional-light registration behavior for entities with `DirectionalLight`
- **AND** enabled directional lights contribute a registration event through the backend runtime adapter

---

### Requirement: current render-spec coverage reflects implemented binding and adapter tests
Current stdlib render coverage SHALL reflect the binding and runtime-adapter tests that exist in the shipped backends rather than promising broader behavioral coverage than is currently implemented.

#### Scenario: Recognized sprite and animation binding is covered in cpp-entt
- **WHEN** the cpp-entt backend test suite runs
- **THEN** it verifies that recognized sprite-renderer and animated-sprite extern systems bind to the EnTT backend runtime adapters

#### Scenario: Representative render adapter behavior is covered in runtime stdlib tests
- **WHEN** runtime stdlib tests run against the shipped backend runtime adapters
- **THEN** they verify representative sprite submission, mesh submission, point-light registration, and missing-asset diagnostics through backend adapter entry points
