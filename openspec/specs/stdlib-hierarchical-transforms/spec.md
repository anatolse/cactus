## Purpose

Define stdlib support for entity transform hierarchies, including the `parent` trait, local/world transform traits for `flat` and `volume` profiles, parent-chain propagation and recursive descendant deletion via backend-bound extern rules, and scratch-storage allocator discipline.

## Requirements

### Requirement: hierarchy relationships are represented by a parent trait
The stdlib SHALL provide a trait for parent-child relationships with a field `parent: entity_id`. An entity with no parent relationship applied SHALL be treated as a root entity.

#### Scenario: parent trait stores an entity reference
- **WHEN** an entity applies `Parent`
- **THEN** it exposes a `parent: entity_id` field that can reference another entity

#### Scenario: root entity omits parent trait
- **WHEN** an entity does not apply `Parent`
- **THEN** hierarchy rules treat it as a root entity

### Requirement: flat hierarchy traits distinguish local and world transforms
The `std.transform.flat` module SHALL provide `LocalTransform` and `WorldTransform` traits with fields `position: vec2`, `rotation: float`, and `scale: vec2`.

#### Scenario: flat local transform defaults are authored-space defaults
- **WHEN** an entity applies `std.transform.flat.LocalTransform`
- **THEN** the fields default to position `(0,0)`, rotation `0.0`, and scale `(1,1)`

#### Scenario: flat world transform stores derived values
- **WHEN** an entity applies `std.transform.flat.WorldTransform`
- **THEN** rules and backends may read world-space `position`, `rotation`, and `scale` from it

### Requirement: volume hierarchy traits distinguish local and world transforms
The `std.transform.volume` module SHALL provide `LocalTransform` and `WorldTransform` traits with fields `position: vec3`, `rotation: quat`, and `scale: vec3`.

#### Scenario: volume local transform defaults are authored-space defaults
- **WHEN** an entity applies `std.transform.volume.LocalTransform`
- **THEN** the fields default to position `(0,0,0)`, identity rotation, and scale `(1,1,1)`

#### Scenario: volume world transform stores derived values
- **WHEN** an entity applies `std.transform.volume.WorldTransform`
- **THEN** rules and backends may read world-space `position`, `rotation`, and `scale` from it

### Requirement: stdlib extern rules propagate transforms through parent chains
The stdlib SHALL provide external rules that derive `WorldTransform` from `LocalTransform` and optional `Parent` relationships. For root entities, `WorldTransform` SHALL be derived directly from `LocalTransform`. For `std.transform.volume.WorldTransform`, the parent and local rotations SHALL be composed using normalized quaternion composition, so `WorldTransform.rotation` remains unit-length across repeated propagation rather than accumulating floating-point drift.

#### Scenario: root entity copies local to world
- **WHEN** an entity has `LocalTransform` and `WorldTransform` but no live parent
- **THEN** propagation sets `WorldTransform` equal to `LocalTransform`

#### Scenario: child entity derives world from parent and local
- **WHEN** an entity has `Parent`, `LocalTransform`, and `WorldTransform`, and the parent has a live `WorldTransform`
- **THEN** propagation derives the child `WorldTransform` from the parent world transform composed with the child local transform

#### Scenario: volume rotation propagation stays normalized
- **WHEN** an entity has `Parent`, `std.transform.volume.LocalTransform`, and `std.transform.volume.WorldTransform`, and the parent has a live `std.transform.volume.WorldTransform`
- **THEN** the child's `WorldTransform.rotation` is derived via normalized quaternion composition of the parent's `WorldTransform.rotation` and the child's `LocalTransform.rotation`, and is unit-length

#### Scenario: stale parent is treated as root-equivalent
- **WHEN** an entity has `Parent.parent` set to a stale or missing entity
- **THEN** propagation does not fail and derives `WorldTransform` from the entity's own `LocalTransform`

### Requirement: stdlib extern rules delete descendants recursively
The stdlib SHALL provide an external rule or equivalent backend-recognized hierarchy behavior that recursively destroys all descendants of an entity when that entity is destroyed.

#### Scenario: direct child is deleted with parent
- **WHEN** a parent entity is destroyed and another entity references it through `Parent.parent`
- **THEN** the child entity is destroyed as part of the same cascade

### Requirement: recognized hierarchy extern rules bind to backend-library implementations
Recognized stdlib hierarchy extern rules SHALL bind to concrete backend-library implementations on supported backend/runtime paths rather than emitting incomplete inline project-local behavior. This requirement applies to hierarchy propagation and recursive descendant deletion behavior described by the stdlib transform and parent contracts.

#### Scenario: Flat hierarchy propagation binds to backend library
- **WHEN** a recognized flat transform propagation extern rule is compiled for a supported backend
- **THEN** generated output binds to backend-library hierarchy propagation behavior for `Parent`, `std.transform.flat.LocalTransform`, and `std.transform.flat.WorldTransform`

#### Scenario: Volume hierarchy propagation binds to backend library
- **WHEN** a recognized volume transform propagation extern rule is compiled for a supported backend
- **THEN** generated output binds to backend-library hierarchy propagation behavior for `Parent`, `std.transform.volume.LocalTransform`, and `std.transform.volume.WorldTransform`

#### Scenario: Cascade deletion binds to backend library
- **WHEN** hierarchy-backed descendant deletion is required for entities related by `Parent.parent`
- **THEN** the supported backend uses backend-library recursive deletion behavior rather than ad hoc project-local traversal logic

### Requirement: hierarchy runtime scratch storage follows backend allocator discipline
When performance-critical generated runtime hierarchy behavior requires dynamic scratch storage, the implementation SHALL use `std::pmr` containers/resources rather than default-allocator standard containers.

#### Scenario: Recursive destroy scratch storage uses pmr
- **WHEN** the backend runtime needs temporary storage to track active recursive destroy state
- **THEN** that storage is implemented with `std::pmr` resources/containers or an allocation-free equivalent

### Requirement: world_position returns the live entity's position independent of editor usage
`std.transform.flat.world_position(of: entity_id)` and `std.transform.volume.world_position(of: entity_id)` SHALL return the target entity's live `WorldTransform.position` whenever that entity is valid and carries a `WorldTransform` of the corresponding dimensionality, for any compiled program — not only programs that also import `std.editor`. The zero vector SHALL be returned only when the target entity is invalid or has no matching `WorldTransform`.

#### Scenario: Non-editor program reads a live entity's position
- **WHEN** a program that does not use `std.editor` calls `world_position(of: some_valid_entity)` on an entity carrying `WorldTransform`
- **THEN** the call returns that entity's actual `WorldTransform.position`, not the zero vector

#### Scenario: Editor-using program still reads live positions
- **WHEN** a program that uses `std.editor` calls `world_position(of: some_valid_entity)`
- **THEN** the call returns that entity's actual `WorldTransform.position`, matching non-editor programs

#### Scenario: Invalid or missing entity falls back to zero
- **WHEN** `world_position` is called with an entity that is invalid, or that lacks the corresponding `WorldTransform`
- **THEN** the call returns the zero vector for that dimensionality