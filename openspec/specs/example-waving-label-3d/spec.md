# example-waving-label-3d Specification

## Purpose
Define the curated 3D text-label example that demonstrates backend-backed `std.render.text` rendering with formatted elapsed-time text and volume-world quaternion animation.

## Requirements

### Requirement: example-waving-label-3d is a valid compilable Cactus module
The example SHALL be a single `.cactus` file in `examples/waving-label-3d/` that compiles successfully through the Cactus compiler and the cpp-entt backend without errors. It SHALL use only `std.math`, `std.math.quat`, `std.text`, `std.transform.volume`, and `std.render.text`.

#### Scenario: Example compiles without errors
- **WHEN** the Cactus compiler processes `examples/waving-label-3d/waving_label_3d.cactus` with the cpp-entt backend
- **THEN** compilation succeeds with no errors or unresolved extern rules

#### Scenario: Example is registered as a CMake build target
- **WHEN** the project CMake configuration is processed
- **THEN** `waving-label-3d` is available as a build target alongside the existing example targets

---

### Requirement: example-waving-label-3d demonstrates TextLabel on a volume-world entity
The example SHALL declare at least one `unit` with `std.transform.volume.WorldTransform` and `std.render.text.TextLabel`. The label plane IS the visual element; no separate mesh or shape trait is required.

#### Scenario: Unit carries volume WorldTransform and TextLabel
- **WHEN** the example module is parsed
- **THEN** a unit exists that applies `std.transform.volume.WorldTransform` and `TextLabel` without any additional mesh or shape trait

---

### Requirement: example-waving-label-3d demonstrates elapsed-time formatting via std.text.format
The example SHALL use `std.text.format` with a `"{:02}:{:02}"` or equivalent format string to produce a zero-padded MM:SS elapsed-time string and assign it to `TextLabel.text` each tick.

#### Scenario: text field is updated with formatted elapsed time each tick
- **WHEN** a tick event fires in the example
- **THEN** a rule reads accumulated elapsed time, computes integer minutes and seconds, and assigns the formatted string to `TextLabel.text`

---

### Requirement: example-waving-label-3d demonstrates compound quaternion rotation waving over time
The example SHALL use `std.math.quat.from_axis_angle` on at least two distinct axes (e.g., Y and X) and `std.math.quat.multiply` to compose them, producing a compound oscillating rotation assigned to `WorldTransform.rotation`. The result SHALL be that the text plane is rendered at orientations non-orthogonal to the view direction throughout the animation cycle.

#### Scenario: Rotation uses compound quat from two axes
- **WHEN** the wave rule runs
- **THEN** it calls `quat.from_axis_angle` at least twice with different axis vectors and combines the results with `quat.multiply` before assigning to `WorldTransform.rotation`

#### Scenario: Text plane passes through non-orthogonal orientations during animation
- **WHEN** the example runs over at least one full oscillation cycle
- **THEN** `WorldTransform.rotation` takes values that cause the text plane's normal to deviate from the camera's view direction — i.e., the surface is not always perpendicular to the line of sight

---

### Requirement: example-waving-label-3d uses WorldTransform.scale to define physical label size
The example SHALL set `WorldTransform.scale` to explicitly size the text plane in world units (e.g., width × height), demonstrating that physical size and font quality (`font_size`) are independent controls.

#### Scenario: WorldTransform.scale sets the label's physical dimensions
- **WHEN** the example module is parsed
- **THEN** the unit's `WorldTransform` block sets `scale` to a non-unit vec3 (e.g., `vec3(3.0, 1.5, 1.0)`) expressing the desired width and height of the label in world units