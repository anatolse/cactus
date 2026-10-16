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

### Requirement: Entity-listing queries use creation order
`std.query.first[...]()` SHALL return the earliest-created live entity that matches its filter, or a stale `entity_id` when none matches. `std.query.all[...]()` SHALL return every live matching entity in stable creation order. Neither result SHALL depend on the backend's storage order, including after entities were destroyed.

#### Scenario: First returns the oldest match after a destroy
- **WHEN** entities A, B and C with trait `Enemy` are created in that order, A is destroyed, and D with `Enemy` is created
- **THEN** `query.first[Enemy]()` returns B

#### Scenario: All lists matches in creation order
- **WHEN** the same entities exist as above
- **THEN** `query.all[Enemy]()` returns B, C, D in that order

### Requirement: Hierarchy snapshots handle any depth
`hierarchy_preorder` and `hierarchy_postorder` SHALL visit each matching entity once and SHALL complete for a hierarchy of any depth. A stale parent SHALL be treated as a missing parent.

#### Scenario: Deep chain snapshot
- **WHEN** a chain of 100000 entities carrying `Node` each has the previous one as parent
- **THEN** `query.hierarchy_preorder[Node]()` returns all 100000 entities root first, without exhausting the stack

#### Scenario: Stale parent makes a root
- **WHEN** a matching entity's parent was destroyed and its handle is stale
- **THEN** the entity is a root of the snapshot

### Requirement: Spatial queries break ties by creation order
The spatial single-entity queries of `std.physics.flat.query` and `std.physics.volume.query` (`nearest`, `raycast`) SHALL return, among candidates with exactly equal distance, the earliest-created one. The spatial list queries (`overlap_box`, `overlap_circle`, `overlap_sphere`) SHALL return their matches in stable creation order. No result SHALL depend on the backend's storage order, including after entities were destroyed, re-added traits, or a world restore.

#### Scenario: Nearest tie returns the oldest entity
- **WHEN** enemies A and B are created in that order, both exactly 5.0 from `p`, and nothing else matches
- **THEN** `query.nearest[Enemy](from = p)` returns A

#### Scenario: Tie result survives storage churn
- **WHEN** enemies A, B and C are equidistant from `p`, created in that order, and `Enemy` is removed from A and added back
- **THEN** `query.nearest[Enemy](from = p)` returns A

#### Scenario: Raycast tie returns the oldest entity
- **WHEN** walls A and B, created in that order, are both first hit by a ray at the same distance
- **THEN** `query.raycast[Wall](origin = o, dir = d, max_dist = 100.0)` returns A

#### Scenario: Overlap list is in creation order
- **WHEN** crates C1, C2 and C3 are created in that order, C1 is destroyed, C4 is created, and all live crates overlap a box
- **THEN** `query.overlap_box[Crate](center = c, size = s)` returns C2, C3, C4
