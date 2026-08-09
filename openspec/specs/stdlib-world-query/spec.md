## Purpose
Define the required behavior of the `std.query`, `std.physics.flat.query`, and `std.physics.volume.query` stdlib namespaces for ECS/world and spatial query expressions.
## Requirements

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
- **WHEN** authored code imports `std.query` and uses `std.query.exists[Boss]()` inside a rule handler
- **THEN** the expression is accepted as a world query and returns `bool`

#### Scenario: Exists query against one trait
- **WHEN** authored code imports `std.query as query` and uses `query.exists[Boss]()` inside a rule handler
- **THEN** the expression is accepted as a world query and returns `bool`

#### Scenario: Count query with positive and negative traits
- **WHEN** authored code uses `query.count[EnemyAI, not Dead]()` inside a rule handler
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

### Requirement: `std.query` provides deterministic direct-child snapshots
The `std.query` namespace SHALL provide a `children(of: entity_id) list[entity_id]` query operation that accepts the ordinary bracketed positive and negative trait predicates. It SHALL return the live direct children whose `Parent.parent` equals the supplied entity and that satisfy the filter, sorted by stable entity creation ordinal. The returned list SHALL be a finite immutable snapshot for bounded foreach.

#### Scenario: Direct children retain stable order
- **WHEN** authored code evaluates `query.children[ui.Node](of = panel)` for three matching children
- **THEN** it receives those direct children in stable creation order and does not include grandchildren

#### Scenario: Stale parent produces empty snapshot
- **WHEN** `children` is called with a stale entity handle
- **THEN** it returns an empty list without an error

### Requirement: `std.query` provides deterministic hierarchy preorder snapshots
The `std.query` namespace SHALL provide `hierarchy_preorder() list[entity_id]` with ordinary bracketed trait predicates. It SHALL return every matching live entity exactly once as a filtered forest in parent-before-descendant order. A matching entity whose direct parent is absent, stale, or does not satisfy the positive/negative filter SHALL be a root of that filtered forest. Roots and siblings SHALL use stable creation ordinal.

#### Scenario: Preorder arranges parent before descendant
- **WHEN** a matching forest contains root A, children B and C, and B's child D
- **THEN** `query.hierarchy_preorder[Marker]()` returns A, B, D, C

#### Scenario: Nonmatching parent creates filtered root
- **WHEN** a matching Node has a live parent that does not carry Node
- **THEN** the Node appears as a root in `hierarchy_preorder[Node]()`

### Requirement: `std.query` provides deterministic hierarchy postorder snapshots
The `std.query` namespace SHALL provide `hierarchy_postorder() list[entity_id]` with ordinary bracketed trait predicates. It SHALL return the same filtered forest membership and stable sibling order as hierarchy_preorder but with every descendant before its matching parent.

#### Scenario: Postorder measures descendants before parent
- **WHEN** a matching forest contains root A, children B and C, and B's child D
- **THEN** `query.hierarchy_postorder[Marker]()` returns D, B, C, A

### Requirement: hierarchy snapshots are total in the presence of invalid relations
Hierarchy queries SHALL terminate for every finite live registry state. Stale parent references SHALL be treated as missing parents. A cycle SHALL not recurse indefinitely or duplicate entities; each cycle member SHALL appear exactly once in deterministic creation order as a root-equivalent component, and the runtime SHALL make the invalid relation diagnosable.

#### Scenario: Cyclic Parent relation terminates
- **WHEN** runtime mutation creates a Parent cycle among three matching entities
- **THEN** preorder and postorder queries return finite deterministic snapshots containing each entity once
