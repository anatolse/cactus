## Requirements

### Requirement: hierarchy relationships are represented by a parent trait
The stdlib SHALL provide a trait for parent-child relationships with a field `parent: entity_id`. An entity with no parent relationship applied SHALL be treated as a root entity.

#### Scenario: parent trait stores an entity reference
- **WHEN** an entity applies `Parent`
- **THEN** it exposes a `parent: entity_id` field that can reference another entity

#### Scenario: root entity omits parent trait
- **WHEN** an entity does not apply `Parent`
- **THEN** hierarchy systems treat it as a root entity

### Requirement: flat hierarchy traits distinguish local and world transforms
The `std.transform.flat` module SHALL provide `LocalTransform` and `WorldTransform` traits with fields `position: vec2`, `rotation: float`, and `scale: vec2`.

#### Scenario: flat local transform defaults are authored-space defaults
- **WHEN** an entity applies `std.transform.flat.LocalTransform`
- **THEN** the fields default to position `(0,0)`, rotation `0.0`, and scale `(1,1)`

#### Scenario: flat world transform stores derived values
- **WHEN** an entity applies `std.transform.flat.WorldTransform`
- **THEN** systems and backends may read world-space `position`, `rotation`, and `scale` from it

### Requirement: volume hierarchy traits distinguish local and world transforms
The `std.transform.volume` module SHALL provide `LocalTransform` and `WorldTransform` traits with fields `position: vec3`, `rotation: quat`, and `scale: vec3`.

#### Scenario: volume local transform defaults are authored-space defaults
- **WHEN** an entity applies `std.transform.volume.LocalTransform`
- **THEN** the fields default to position `(0,0,0)`, identity rotation, and scale `(1,1,1)`

#### Scenario: volume world transform stores derived values
- **WHEN** an entity applies `std.transform.volume.WorldTransform`
- **THEN** systems and backends may read world-space `position`, `rotation`, and `scale` from it

### Requirement: stdlib extern systems propagate transforms through parent chains
The stdlib SHALL provide external systems that derive `WorldTransform` from `LocalTransform` and optional `Parent` relationships. For root entities, `WorldTransform` SHALL be derived directly from `LocalTransform`.

#### Scenario: root entity copies local to world
- **WHEN** an entity has `LocalTransform` and `WorldTransform` but no live parent
- **THEN** propagation sets `WorldTransform` equal to `LocalTransform`

#### Scenario: child entity derives world from parent and local
- **WHEN** an entity has `Parent`, `LocalTransform`, and `WorldTransform`, and the parent has a live `WorldTransform`
- **THEN** propagation derives the child `WorldTransform` from the parent world transform composed with the child local transform

#### Scenario: stale parent is treated as root-equivalent
- **WHEN** an entity has `Parent.parent` set to a stale or missing entity
- **THEN** propagation does not fail and derives `WorldTransform` from the entity's own `LocalTransform`

### Requirement: stdlib extern systems delete descendants recursively
The stdlib SHALL provide an external system or equivalent backend-recognized hierarchy behavior that recursively destroys all descendants of an entity when that entity is destroyed.

#### Scenario: direct child is deleted with parent
- **WHEN** a parent entity is destroyed and another entity references it through `Parent.parent`
- **THEN** the child entity is destroyed as part of the same cascade