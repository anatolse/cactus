## Purpose

Define the `std.transform` stdlib module's 2D (`flat`) and 3D (`volume`) hierarchical transform traits, including their mutual exclusivity on a single entity.

## Requirements

### Requirement: std.transform.flat provides 2D hierarchical transform traits
The `std.transform.flat` module SHALL provide `pub trait LocalTransform` and `pub trait WorldTransform` with `vec2` position, `float` rotation (angle in radians), and `vec2` scale. All fields SHALL have sensible defaults (origin position, zero rotation, unit scale). `LocalTransform` stores authored transform state relative to the parent; `WorldTransform` stores derived world-space state.

#### Scenario: LocalTransform trait fields for 2D
- **WHEN** an entity has `std.transform.flat.LocalTransform` applied
- **THEN** the entity has fields `position: vec2`, `rotation: float`, `scale: vec2` with defaults `(0,0)`, `0.0`, `(1,1)`

#### Scenario: WorldTransform trait fields for 2D
- **WHEN** an entity has `std.transform.flat.WorldTransform` applied
- **THEN** the entity has fields `position: vec2`, `rotation: float`, `scale: vec2` with defaults `(0,0)`, `0.0`, `(1,1)`

#### Scenario: Used in 2D unit
- **WHEN** a unit imports `use std.transform.flat as tr` and applies `tr.LocalTransform` and `tr.WorldTransform`
- **THEN** the compiler accepts the unit and rules may reference the hierarchy transform traits in filters

### Requirement: std.transform.volume provides 3D hierarchical transform traits
The `std.transform.volume` module SHALL provide `pub trait LocalTransform` and `pub trait WorldTransform` with `vec3` position, `quat` rotation, and `vec3` scale. All fields SHALL have sensible defaults (origin position, identity rotation, unit scale). `LocalTransform` stores authored transform state relative to the parent; `WorldTransform` stores derived world-space state.

#### Scenario: LocalTransform trait fields for 3D
- **WHEN** an entity has `std.transform.volume.LocalTransform` applied
- **THEN** the entity has fields `position: vec3`, `rotation: quat`, `scale: vec3` with defaults `(0,0,0)`, identity quat, `(1,1,1)`

#### Scenario: WorldTransform trait fields for 3D
- **WHEN** an entity has `std.transform.volume.WorldTransform` applied
- **THEN** the entity has fields `position: vec3`, `rotation: quat`, `scale: vec3` with defaults `(0,0,0)`, identity quat, `(1,1,1)`

#### Scenario: Used in 3D unit
- **WHEN** a unit imports `use std.transform.volume as tr` and applies `tr.LocalTransform` and `tr.WorldTransform`
- **THEN** the compiler accepts the unit and rules may reference the hierarchy transform traits in filters

### Requirement: flat and volume hierarchy transform traits are not simultaneously applicable to one entity
The semantic analyzer SHALL reject a unit or template that mixes flat and volume hierarchy transform traits on the same entity. These traits are spatially incompatible — an entity cannot simultaneously have 2D and 3D local/world transform state.

#### Scenario: Both flat and volume hierarchy transforms applied is an error
- **WHEN** a unit applies `tr_flat.LocalTransform` and `tr_volume.WorldTransform`
- **THEN** the compiler reports an error indicating that flat and volume hierarchy transform traits cannot be applied to the same entity

#### Scenario: Mixing volume world transform with mesh render is valid
- **WHEN** a unit applies `tr_volume.WorldTransform` and `std.render.meshes.Renderer`
- **THEN** the compiler accepts this because render and transform modules remain orthogonal
