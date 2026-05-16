## ADDED Requirements

### Requirement: `std.query` provides ECS/world query expressions
The stdlib SHALL provide a `std.query` namespace for world/ECS query expressions. The namespace SHALL include at least `exists`, `count`, `first`, and `all` query operations that accept bracketed trait-filter arguments.

Query operations are declared in stdlib modules as ordinary `extern func` symbols. The bracketed trait filter is supplied at the call site rather than encoded as a declared value parameter.

Illustrative declaration examples:

```cactus
module std.query

pub extern func exists() bool
pub extern func count() int
pub extern func first() entity_id
pub extern func all() list[entity_id]
pub extern func parent(of: entity_id) entity_id

module std.physics.flat.query

pub extern func nearest(from: vec2) entity_id
pub extern func overlap_box(center: vec2, size: vec2) list[entity_id]
pub extern func overlap_circle(center: vec2, radius: float) list[entity_id]
pub extern func raycast(origin: vec2, dir: vec2, max_dist: float) entity_id

module std.physics.volume.query

pub extern func nearest(from: vec3) entity_id
pub extern func overlap_box(center: vec3, size: vec3) list[entity_id]
pub extern func overlap_sphere(center: vec3, radius: float) list[entity_id]
pub extern func raycast(origin: vec3, dir: vec3, max_dist: float) entity_id
```

#### Scenario: Query function called through module path
- **WHEN** authored code imports `std.query` and uses `std.query.exists[Boss]()` inside a system handler
- **THEN** the expression is accepted as a world query and returns `bool`

#### Scenario: Exists query against one trait
- **WHEN** authored code imports `std.query as query` and uses `query.exists[Boss]()` inside a system handler
- **THEN** the expression is accepted as a world query and returns `bool`

#### Scenario: Count query with positive and negative traits
- **WHEN** authored code uses `query.count[EnemyAI, not Dead]()` inside a system handler
- **THEN** the expression returns the number of live entities that have `EnemyAI` and do not have `Dead`

### Requirement: Query filter brackets support positive and negative trait predicates
Bracketed query filters SHALL support one or more trait predicates. A bare trait name is a positive predicate. `not TraitName` is a negative predicate. The result set SHALL be the intersection of all positive predicates minus all negative predicates.

#### Scenario: Multi-trait intersection
- **WHEN** authored code uses `query.first[Interactable, Highlighted]()`
- **THEN** the query matches only entities that currently have both `Interactable` and `Highlighted`

#### Scenario: Negative trait excludes entities
- **WHEN** authored code uses `query.all[Enemy, not Invisible]()`
- **THEN** entities with `Enemy` but also `Invisible` are excluded from the result

### Requirement: Entity-returning world queries use total `entity_id` semantics
World query expressions that select a single entity SHALL return `entity_id`. If no live entity matches the query, the result SHALL be a stale/non-live `entity_id` handle value rather than a null-like or optional result.

#### Scenario: First query returns entity handle when match exists
- **WHEN** authored code uses `query.first[PlayerOwned]()` and at least one live entity has `PlayerOwned`
- **THEN** the expression returns an `entity_id` handle for one matching entity

#### Scenario: First query returns stale handle when no match exists
- **WHEN** authored code uses `query.first[Boss]()` and no live entity has `Boss`
- **THEN** the expression returns an `entity_id` value that behaves as a stale handle under total entity-id semantics

### Requirement: `std.query` supports parent relationship lookup
The `std.query` namespace SHALL provide a `parent` query operation for parent-child relationship lookup. `parent` takes an entity argument through an ordinary named parameter rather than a trait-filter bracket.

#### Scenario: Parent query returns direct parent
- **WHEN** authored code uses `query.parent(of = child_id)` and `child_id` currently has a parent relationship
- **THEN** the expression returns that parent as `entity_id`

#### Scenario: Parent query returns stale handle when no parent exists
- **WHEN** authored code uses `query.parent(of = root_id)` and `root_id` has no parent relationship
- **THEN** the expression returns a stale/non-live `entity_id` value

### Requirement: Spatial query namespaces support trait-filtered query expressions
The stdlib SHALL provide `std.physics.flat.query` and `std.physics.volume.query` namespaces for spatial queries. Spatial queries SHALL accept the same bracketed trait-filter grammar as `std.query` and SHALL intersect the spatial test with the trait filter.

#### Scenario: Flat nearest query uses spatial and trait predicates
- **WHEN** authored code imports `std.physics.flat.query as query` and uses `query.nearest[Transform, Enemy](from = player_pos)`
- **THEN** the query considers only entities satisfying the spatial lookup and the traits `Transform` and `Enemy`

#### Scenario: Flat overlap query supports negative filter
- **WHEN** authored code uses `query.overlap_box[Pickup, not Collected](center = p, size = s)`
- **THEN** the returned entity set excludes entities that already have `Collected`

#### Scenario: Flat circle overlap query supports trait filters
- **WHEN** authored code uses `query.overlap_circle[Enemy, not Dead](center = p, radius = 64.0)`
- **THEN** the returned entity set contains only entities inside the circle that satisfy the listed trait predicates

#### Scenario: Raycast query supports trait filters
- **WHEN** authored code uses `query.raycast[Wall, not Trigger](origin = p, dir = d, max_dist = 100.0)`
- **THEN** the raycast considers only hits satisfying the listed trait predicates

### Requirement: Spatial nearest queries return total `entity_id` values
Spatial query expressions that select a single entity, including `nearest`, SHALL return `entity_id` with the same total-handle semantics as ECS `first` queries.

#### Scenario: Nearest returns matching entity
- **WHEN** `query.nearest[Transform, Enemy](from = p)` has at least one live matching entity
- **THEN** the expression returns that nearest matching entity as `entity_id`

#### Scenario: Nearest returns stale handle on empty result
- **WHEN** `query.nearest[Transform, Enemy](from = p)` has no live matching entity
- **THEN** the expression returns a stale/non-live `entity_id` value