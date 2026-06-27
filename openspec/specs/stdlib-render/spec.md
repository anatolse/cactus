# stdlib-render Specification

## Purpose
Define the shipped stdlib rendering surface and the current backend-backed runtime behavior for sprite, mesh, and light-related rendering features.

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

#### Scenario: Visible mesh is submitted
- **WHEN** a supported backend schedules `MeshRenderer` for an entity with `std.transform.volume.WorldTransform`, `Renderer.visible = true`, and resolvable mesh/material handles
- **THEN** the backend records one mesh submission for that entity using the entity's world transform and renderer asset handles

#### Scenario: Invisible mesh skips drawing
- **WHEN** mesh rendering is invoked with `Renderer.visible = false`
- **THEN** the backend does not draw that entity in the mesh pass or record a mesh submission for it

#### Scenario: Point lights are backend-backed
- **WHEN** a program imports `std.render.meshes` and the recognized `PointLightSystem` extern system is scheduled
- **THEN** generated output binds to backend-library point-light registration behavior for entities with `std.transform.volume.WorldTransform` and `PointLight`
- **AND** enabled point lights contribute a registration event through the backend runtime adapter

#### Scenario: Directional lights are backend-backed
- **WHEN** a program imports `std.render.meshes` and the recognized `DirectionalLightSystem` extern system is scheduled
- **THEN** generated output binds to backend-library directional-light registration behavior for entities with `DirectionalLight`
- **AND** enabled directional lights contribute a registration event through the backend runtime adapter

### Requirement: Backend-backed point lights contribute to backend-backed mesh shading
For supported backend-owned 3D render paths, recognized `std.render.meshes.PointLightSystem` behavior SHALL provide per-frame point-light inputs consumed by backend-backed mesh rendering rather than only non-visual registration accounting.

#### Scenario: Enabled point lights affect mesh-rendering inputs
- **WHEN** a supported backend schedules `PointLightSystem` and `MeshRenderer` in the same frame for enabled lights and visible meshes
- **THEN** the backend-backed mesh rendering path receives point-light data from the registered lights for that frame

#### Scenario: Disabled point lights do not affect mesh-rendering inputs
- **WHEN** a `std.render.meshes.PointLight` has `enabled = false`
- **THEN** the backend does not contribute that light to the frame's mesh-lighting inputs

### Requirement: std.render.meshes point-light behavior supports representative multi-light scenes
The shipped `std.render.meshes.PointLight` surface SHALL be sufficient for representative multi-light scenes where at least two enabled point lights contribute to the same backend-backed mesh render frame.

#### Scenario: Two point lights can coexist in one frame
- **WHEN** two enabled `PointLight` entities are present in a supported backend-backed mesh scene
- **THEN** both lights are eligible to contribute to that frame's mesh-lighting behavior

---

### Requirement: current render-spec coverage reflects implemented binding and adapter tests
Current stdlib render coverage SHALL reflect the binding and runtime-adapter tests that exist in the shipped backends rather than promising broader behavioral coverage than is currently implemented.

#### Scenario: Recognized sprite and animation binding is covered in cpp-entt
- **WHEN** the cpp-entt backend test suite runs
- **THEN** it verifies that recognized sprite-renderer and animated-sprite extern systems bind to the EnTT backend runtime adapters

#### Scenario: Representative render adapter behavior is covered in runtime stdlib tests
- **WHEN** runtime stdlib tests run against the shipped backend runtime adapters
- **THEN** they verify representative sprite submission, mesh submission, point-light registration, and missing-asset diagnostics through backend adapter entry points

---

### Requirement: ShapeRenderer renders entities in world space using the active camera
The `ShapeRenderer` extern system SHALL wrap its raylib draw calls in `BeginMode2D(get_active_camera_2d())` / `EndMode2D()`. Entity positions (`WorldTransform.position`) and sizes (`Shape.size`) are in world-unit coordinates; the camera transform maps them to screen pixels. When the active camera is identity (zoom=1, no offset), the behavior is identical to pixel-space rendering (backwards-compatible).

#### Scenario: World-unit entity rendered at correct screen position
- **WHEN** the active camera has zoom=64 and offset={400,300} (world origin at screen center)
- **WHEN** an entity has `WorldTransform.position = {1.0, 0.0}` and `Shape.size = {1.0, 1.0}` with `ShapeType.Rectangle`
- **THEN** a 64×64 pixel rectangle is drawn at screen position (464, 300)

#### Scenario: Pixel-space module unaffected
- **WHEN** no Camera entity exists (identity active camera, zoom=1)
- **WHEN** an entity has `WorldTransform.position = {100.0, 200.0}` and `Shape.size = {32.0, 32.0}`
- **THEN** a 32×32 pixel rectangle is drawn at screen position (100, 200) — same as before this change

#### Scenario: Hidden shape not drawn
- **WHEN** a Shape entity has `visible = false`
- **THEN** no rectangle is drawn for that entity (unchanged behavior)
