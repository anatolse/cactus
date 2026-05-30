# example-waving-label-2d Specification

## Purpose
Define the curated 2D text-label example that demonstrates backend-backed `std.render.text` rendering with formatted elapsed-time text and flat-world transform animation.

## Requirements

### Requirement: example-waving-label-2d is a valid compilable Cactus module
The example SHALL be a single `.cactus` file in `examples/waving-label-2d/` that compiles successfully through the Cactus compiler and the cpp-entt backend without errors. It SHALL use only `std.math`, `std.text`, `std.transform.flat`, `std.render.shapes`, and `std.render.text`.

#### Scenario: Example compiles without errors
- **WHEN** the Cactus compiler processes `examples/waving-label-2d/waving_label_2d.cactus` with the cpp-entt backend
- **THEN** compilation succeeds with no errors or unresolved extern systems

#### Scenario: Example is registered as a CMake build target
- **WHEN** the project CMake configuration is processed
- **THEN** `waving-label-2d` is available as a build target alongside the existing example targets

---

### Requirement: example-waving-label-2d demonstrates TextLabel on a flat-world entity
The example SHALL declare at least one `unit` with `std.transform.flat.WorldTransform`, `std.render.shapes.Shape`, and `std.render.text.TextLabel` on the same entity. The `Shape` provides a visible square; the `TextLabel` provides the rendered text label.

#### Scenario: Square entity carries both Shape and TextLabel
- **WHEN** the example module is parsed
- **THEN** a unit exists that applies both `Shape` (Rectangle) and `TextLabel` to the same entity

---

### Requirement: example-waving-label-2d demonstrates elapsed-time formatting via std.text.format
The example SHALL use `std.text.format` with a `"{:02}:{:02}"` or equivalent format string to produce a zero-padded MM:SS elapsed-time string and assign it to the `TextLabel.text` field each tick.

#### Scenario: text field is updated with formatted elapsed time each tick
- **WHEN** a tick event fires in the example
- **THEN** a system reads accumulated elapsed time, computes integer minutes and seconds via integer division and modulo, and assigns `format("{:02}:{:02}", mm, ss)` to `TextLabel.text`

---

### Requirement: example-waving-label-2d demonstrates position and rotation waving over time
The example SHALL include a system that accumulates `tick.dt` into an elapsed float and uses `std.math.sin` to drive both `WorldTransform.position` (left-right oscillation) and `WorldTransform.rotation` (angular oscillation) of the entity. The waving SHALL result in the text label being rendered at varying screen positions and non-zero rotation angles each frame.

#### Scenario: Position oscillates with sin of elapsed time
- **WHEN** the wave system runs with accumulated elapsed time `t`
- **THEN** `WorldTransform.position.x` is set to `CENTER_X + sin(t * WAVE_SPEED) * AMPLITUDE` (or equivalent)

#### Scenario: Rotation oscillates with sin of elapsed time
- **WHEN** the wave system runs with accumulated elapsed time `t`
- **THEN** `WorldTransform.rotation` is set to a non-zero value driven by `sin` so the text label visibly rotates