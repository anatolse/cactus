# render-picture-layer Specification

## Purpose

Define the picture layer: frame-local drawn pose and drawn color values that Cactus rules project and that renderers draw in place of the simulation's values, so visual effects never change the simulation.

## Requirements

### Requirement: Drawn pose and drawn color traits
`std.transform.flat` SHALL declare `pub trait DrawnPose` with `position: vec2`, `rotation: float` and `scale: vec2`. `std.transform.volume` SHALL declare `pub trait DrawnPose` with `position: vec3`, `rotation: quat` and `scale: vec3`. `std.render.picture` SHALL declare `pub trait DrawnColor` with `tint: color = #FFFFFFFF` and `glow: color = #00000000`. Any rule SHALL be able to `project` these traits.

#### Scenario: Projection lasts one frame
- **WHEN** a rule projects `DrawnPose` for an entity in one frame and not in the next
- **THEN** the entity has a `DrawnPose` during the first frame's render and none during the second

### Requirement: Picture layer group
`std.render.layers` SHALL declare a group `picture` in `core.render` that every drawing layer runs after.

#### Scenario: Picture rules run before renderers
- **WHEN** a rule in `group: layers.picture` projects `DrawnPose` on `core.render`
- **THEN** every stdlib renderer of that frame sees the projected value

### Requirement: Renderers draw the drawn pose when it exists
A stdlib renderer that draws an entity at its `WorldTransform` SHALL draw it at the entity's `DrawnPose` of the same flavour when the entity has one this frame, and at its `WorldTransform` otherwise. This SHALL hold for sprites, shapes, 2D and 3D text labels, meshes, models and point lights. A renderer SHALL NOT write `WorldTransform`, its own trait, `DrawnPose` or `DrawnColor`.

#### Scenario: Shifted shape
- **WHEN** a shape's `WorldTransform.position` is (100, 100) and its projected `DrawnPose.position` is (104, 100)
- **THEN** the shape is drawn at (104, 100) and `WorldTransform.position` is still (100, 100)

#### Scenario: Flat and volume in one program
- **WHEN** a program projects a flat `DrawnPose` for a shape and a volume `DrawnPose` for a model
- **THEN** each is drawn at its own projected pose

### Requirement: Renderers apply the drawn color when it exists
When the entity has a `DrawnColor` this frame, a stdlib renderer with a color field of its own SHALL use that color times `tint` plus `glow`, per channel in float with no clamp; otherwise its own color. The result SHALL reach the output as its own color does: one 8-bit conversion for an 8-bit consumer, raw for a light uniform. This SHALL hold for the renderers above and for directional lights.

#### Scenario: Tinted and flashed
- **WHEN** a shape's color is `#808080FF` and its `DrawnColor` has `tint = #FFFFFF80` and `glow = #40000000`
- **THEN** it is drawn with color `#C0808080` within one step per channel, and `Shape.color` is unchanged

#### Scenario: Flash above white
- **WHEN** a white shape and a white point light on one entity get a `DrawnColor` with `glow = #80000000`
- **THEN** the shape is drawn with red 255, and the light's uploaded color has red 1.5 within float rounding

#### Scenario: No picture
- **WHEN** an entity has no `DrawnPose` and no `DrawnColor` this frame
- **THEN** it is drawn exactly as before this capability existed

#### Scenario: Two drawn things on one entity
- **WHEN** an entity carries a red `Shape` and a white `TextLabel` and a `DrawnColor` with `tint = #FFFFFF80`
- **THEN** the shape is drawn half-transparent red and the label half-transparent white

### Requirement: Viewports draw through the camera's drawn pose
A viewport SHALL draw through its volume camera entity's `DrawnPose` when the entity has one this frame, and through its `WorldTransform` otherwise. `std.camera.volume.CameraView` and the functions that read it SHALL use the camera's `WorldTransform` only.

#### Scenario: Shaken camera
- **WHEN** a rule projects a `DrawnPose` for a camera entity, moved 0.1 units from its `WorldTransform`
- **THEN** the viewport's picture is drawn from the moved pose, and `to_screen` returns the same value as with no projection
