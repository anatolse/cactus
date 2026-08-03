# stdlib-render-text Specification

## Purpose
Define the shipped `std.render.text` stdlib surface and backend-backed behavior for flat and volume text labels.

## Requirements

### Requirement: std.render.text exposes a single TextLabel trait
The `std.render.text` module SHALL expose a `TextLabel` trait with the following fields: `text: string` (var, default `""`), `font_size: int` (var, default `32`), `color: color` (var, default `#FFFFFFFF`), `visible: bool` (var, default `true`). No additional fields SHALL be required to select a rendering mode.

#### Scenario: TextLabel trait fields accessible after import
- **WHEN** authored code contains `use std.render.text` and a unit body applies `TextLabel`
- **THEN** the entity has writable fields `text`, `font_size`, `color`, and `visible` with the specified types and defaults

#### Scenario: TextLabel with only text field set compiles
- **WHEN** a unit body applies `TextLabel:` with only `text = "hello"` overriding the default
- **THEN** semantic analysis accepts the declaration and remaining fields take their defaults

---

### Requirement: std.render.text declares TextRenderer2D for flat-world entities
The `std.render.text` module SHALL declare `extern rule TextRenderer2D` with a filter requiring both `std.transform.flat.WorldTransform` and `TextLabel`. This rule SHALL be recognized by the backend as the 2D text rendering path.

#### Scenario: TextRenderer2D declaration matches shipped stdlib
- **WHEN** `std.render.text` is imported
- **THEN** the module declares `extern rule TextRenderer2D` with filter entries `std.transform.flat.WorldTransform` and `TextLabel`

#### Scenario: TextRenderer2D fires only for flat-world entities
- **WHEN** an entity carries `std.transform.flat.WorldTransform` and `TextLabel`
- **THEN** `TextRenderer2D` is scheduled for that entity each frame

#### Scenario: TextRenderer2D does not fire for volume-world entities
- **WHEN** an entity carries `std.transform.volume.WorldTransform` and `TextLabel` but no flat WorldTransform
- **THEN** `TextRenderer2D` is not scheduled for that entity

---

### Requirement: std.render.text declares TextRenderer3D for volume-world entities
The `std.render.text` module SHALL declare `extern rule TextRenderer3D` with a filter requiring both `std.transform.volume.WorldTransform` and `TextLabel`. This rule SHALL be recognized by the backend as the 3D surface text rendering path.

#### Scenario: TextRenderer3D declaration matches shipped stdlib
- **WHEN** `std.render.text` is imported
- **THEN** the module declares `extern rule TextRenderer3D` with filter entries `std.transform.volume.WorldTransform` and `TextLabel`

#### Scenario: TextRenderer3D fires only for volume-world entities
- **WHEN** an entity carries `std.transform.volume.WorldTransform` and `TextLabel`
- **THEN** `TextRenderer3D` is scheduled for that entity each frame

#### Scenario: TextRenderer3D does not fire for flat-world entities
- **WHEN** an entity carries `std.transform.flat.WorldTransform` and `TextLabel` but no volume WorldTransform
- **THEN** `TextRenderer3D` is not scheduled for that entity

---

### Requirement: font_size semantics differ by rendering context
In the 2D path (`TextRenderer2D`), `font_size` is interpreted as screen pixels — the height of rendered characters on screen. In the 3D path (`TextRenderer3D`), `font_size` is interpreted as world units — the height of rendered characters in world space. The trait does not carry a unit qualifier; the rendering context determines the interpretation.

#### Scenario: 2D font_size is screen pixels
- **WHEN** a flat-world entity has `TextLabel` with `font_size = 32`
- **THEN** the 2D backend draws characters approximately 32 pixels tall on screen

#### Scenario: 3D font_size is world units
- **WHEN** a volume-world entity has `TextLabel` with `font_size = 1` and `WorldTransform.scale = vec3(1.0, 1.0, 1.0)`
- **THEN** the 3D backend sizes the text surface so characters are approximately 1 world unit tall

---

### Requirement: invisible TextLabel entities are not rendered
When `TextLabel.visible = false`, the backend SHALL skip drawing for that entity in both the 2D and 3D paths.

#### Scenario: Invisible 2D label skipped
- **WHEN** a flat-world entity has `TextLabel.visible = false`
- **THEN** no text is drawn for that entity in the 2D render pass

#### Scenario: Invisible 3D label skipped
- **WHEN** a volume-world entity has `TextLabel.visible = false`
- **THEN** no text plane is drawn for that entity in the 3D render pass

---

### Requirement: 2D text renders respecting WorldTransform rotation
The 2D backend path SHALL apply `std.transform.flat.WorldTransform.rotation` (in radians) when drawing text, so that the rendered text rotates with the entity.

#### Scenario: Rotated flat entity draws rotated text
- **WHEN** a flat-world entity has `WorldTransform.rotation` set to a non-zero radian angle and a visible `TextLabel`
- **THEN** the 2D backend draws the text rotated by that angle around the entity's world position

---

### Requirement: 3D text renders on a surface following WorldTransform orientation
The 3D backend path SHALL draw text on a plane mesh whose orientation tracks `std.transform.volume.WorldTransform.rotation` (quaternion). The text surface SHALL NOT always face the camera; it SHALL rotate freely with the entity.

#### Scenario: Rotated volume entity draws text on rotated surface
- **WHEN** a volume-world entity has `WorldTransform.rotation` set to a non-identity quaternion and a visible `TextLabel`
- **THEN** the 3D backend draws the text plane rotated accordingly — it does not face the camera as a billboard would

#### Scenario: Identity rotation produces a vertical plane
- **WHEN** a volume-world entity has `WorldTransform.rotation = quat.identity()` and a visible `TextLabel`
- **THEN** the 3D backend draws an upright plane (XY-oriented, normal along +Z)

---

### Requirement: std.render.text exposes a ScreenLabel trait for window-space text
The `std.render.text` module SHALL expose a `pub trait ScreenLabel` with fields `var text: string = ""`, `var position: vec2` (screen pixels, top-left origin), `var font_size: int = 32` (screen pixels), `var color: color = #FFFFFFFF`, and `var visible: bool = true`. The trait SHALL NOT require any `WorldTransform`.

#### Scenario: ScreenLabel fields accessible after import
- **WHEN** authored code contains `use std.render.text` and a unit body applies `ScreenLabel`
- **THEN** the entity has writable fields `text`, `position`, `font_size`, `color`, and `visible` with the specified types and defaults

#### Scenario: ScreenLabel entity needs no WorldTransform
- **WHEN** an entity declares only `ScreenLabel` and no transform trait
- **THEN** semantic analysis accepts the entity and the label renders

---

### Requirement: ScreenLabelRender renders window-space text in any transform flavor
The `std.render.text` module SHALL declare `extern rule ScreenLabelRender` filtered on `ScreenLabel` alone, recognized by the backend as a render-phase rule. The backend SHALL draw visible labels at `position` in window pixel coordinates (top-left origin), after all viewport/world rendering so labels overlay 2D and 3D content. The rule SHALL be functional in programs using either `std.transform.flat` or `std.transform.volume` — unlike `TextRenderer2D`, it SHALL NOT be disabled by the program's `WorldTransform` flavor.

#### Scenario: HUD label over a 3D scene
- **WHEN** a `volume`-flavor program has an entity with `ScreenLabel { text = "hello", position = vec2(16.0, 16.0) }`
- **THEN** the text draws at 16,16 window pixels on top of the rendered 3D scene

#### Scenario: ScreenLabel also works in flat programs
- **WHEN** a `flat`-flavor program has a visible `ScreenLabel` entity
- **THEN** the text draws at its window position over the 2D scene

#### Scenario: Invisible screen label skipped
- **WHEN** an entity's `ScreenLabel.visible` is `false`
- **THEN** no text is drawn for that entity

#### Scenario: Label text updates take effect same frame
- **WHEN** a rule writes `ScreenLabel.text` during the update phase
- **THEN** the label rendered that frame shows the new text