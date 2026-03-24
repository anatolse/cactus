## Requirements

### Requirement: std.transform.flat provides a 2D spatial transform trait
The `std.transform.flat` module SHALL provide a `pub trait Transform` with `vec2` position, `float` rotation (angle in radians), and `vec2` scale. All fields SHALL have sensible defaults (origin position, zero rotation, unit scale).

#### Scenario: Transform trait fields for 2D
- **WHEN** an entity has `std.transform.flat.Transform` applied
- **THEN** the entity has fields: `position: vec2`, `rotation: float`, `scale: vec2` with defaults `(0,0)`, `0.0`, `(1,1)`

#### Scenario: Used in 2D unit
- **WHEN** a unit imports `use std.transform.flat as tr` and applies `tr.Transform`
- **THEN** the compiler accepts the apply block and `Transform` is available for filter clauses in systems

#### Scenario: Unambiguous when only flat is imported
- **WHEN** only `std.transform.flat` is imported (not `std.transform.volume`)
- **THEN** bare `Transform` in `apply:` and `filter:` resolves unambiguously to the flat variant

---

### Requirement: std.transform.volume provides a 3D spatial transform trait
The `std.transform.volume` module SHALL provide a `pub trait Transform` with `vec3` position, `quat` rotation, and `vec3` scale. All fields SHALL have sensible defaults (origin position, identity rotation, unit scale).

#### Scenario: Transform trait fields for 3D
- **WHEN** an entity has `std.transform.volume.Transform` applied
- **THEN** the entity has fields: `position: vec3`, `rotation: quat`, `scale: vec3` with defaults `(0,0,0)`, identity quat, `(1,1,1)`

#### Scenario: Used in 3D unit
- **WHEN** a unit imports `use std.transform.volume as tr` and applies `tr.Transform`
- **THEN** the compiler accepts the apply block and `Transform` fields are accessible in systems as `t.position`, `t.rotation`, `t.scale`

---

### Requirement: flat and volume Transform traits are not simultaneously applicable to one entity
The semantic analyzer SHALL reject a unit or template that applies both `std.transform.flat.Transform` and `std.transform.volume.Transform`. These traits are spatially incompatible — an entity cannot simultaneously have both a `vec2` and a `vec3` position.

#### Scenario: Both applied is an error
- **WHEN** a unit applies both `tr_flat.Transform` and `tr_volume.Transform`
- **THEN** the compiler reports an error: "Cannot apply both std.transform.flat.Transform and std.transform.volume.Transform to the same entity"

#### Scenario: Mixing flat with volume render is valid
- **WHEN** a unit applies `tr_volume.Transform` (3D position) and `std.render.sprites.Renderer` (sprite rendering)
- **THEN** the compiler accepts this — transform and render sub-modules are orthogonal choices
