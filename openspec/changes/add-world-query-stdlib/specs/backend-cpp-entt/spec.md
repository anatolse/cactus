## ADDED Requirements

### Requirement: cpp-entt backend lowers world query expressions to registry queries
The cpp-entt backend SHALL compile recognized `std.query` expressions to direct `entt::registry` queries rather than reflective or name-based lookup logic. The backend SHALL distinguish these from ordinary extern-function calls using semantic metadata that marks the call as a recognized query expression and carries the parsed trait filters.

#### Scenario: Exists query compiles to registry/view check
- **WHEN** authored code uses `query.exists[Boss]()`
- **THEN** the backend generates code that checks whether any live entity currently satisfies the `Boss` filter

#### Scenario: Plain extern func call does not use query lowering
- **WHEN** authored code uses `std.math.sqrt(x)`
- **THEN** the backend lowers it as an ordinary extern-function call and does not attempt registry-query code generation

#### Scenario: Query lowering uses call-site filter metadata
- **WHEN** authored code uses `std.query.count[EnemyAI, not Dead]()`
- **THEN** the backend uses the resolved query callee together with the attached positive and negative trait filters to generate the registry query

#### Scenario: Count query compiles to filtered iteration
- **WHEN** authored code uses `query.count[EnemyAI, not Dead]()`
- **THEN** the backend generates code that counts live registry entities matching the positive and negative trait filter

#### Scenario: Parent query lowers to relationship lookup
- **WHEN** authored code uses `query.parent(of = child_id)`
- **THEN** the backend generates code that looks up the direct parent relationship for `child_id` and returns it as `entity_id`

#### Scenario: Parent query returns stale handle when relationship missing
- **WHEN** authored code uses `query.parent(of = root_id)` and no parent relationship exists
- **THEN** the generated code returns an `entity_id` value for which `registry.valid(id)` is false or which otherwise behaves as stale/non-live under total handle semantics

### Requirement: cpp-entt backend returns stale handle sentinel for empty single-entity queries
For recognized single-entity query expressions such as `first` and `nearest`, the cpp-entt backend SHALL return an `entity_id` value that behaves as stale/non-live when no live entity matches.

#### Scenario: First query on empty result returns stale handle
- **WHEN** `query.first[Boss]()` executes with no live matching entity
- **THEN** the generated code returns an `entity_id` value for which `registry.valid(id)` is false

#### Scenario: Nearest query on empty result returns stale handle
- **WHEN** `query.nearest[Transform, Enemy](from = p)` executes with no live matching entity
- **THEN** the generated code returns an `entity_id` value for which `registry.valid(id)` is false

### Requirement: cpp-entt backend lowers spatial queries to geometry-filtered entity searches
The cpp-entt backend SHALL compile recognized `std.physics.flat.query` and `std.physics.volume.query` expressions to runtime spatial search logic intersected with the provided trait filters.

#### Scenario: Flat nearest query intersects trait filter
- **WHEN** authored code uses `query.nearest[Transform, Enemy](from = p)`
- **THEN** the generated code searches candidate entities using the flat spatial representation and ignores entities missing `Transform` or `Enemy`

#### Scenario: Flat overlap query excludes negative filter matches
- **WHEN** authored code uses `query.overlap_box[Pickup, not Collected](center = p, size = s)`
- **THEN** the generated code excludes any candidate entity that has `Collected`

#### Scenario: Flat circle overlap query lowers to radius-based search
- **WHEN** authored code uses `query.overlap_circle[Enemy](center = p, radius = r)`
- **THEN** the generated code performs a 2D radius-based spatial search and filters matches by the listed traits

#### Scenario: Volume sphere overlap query lowers to radius-based search
- **WHEN** authored code uses `query.overlap_sphere[Enemy](center = p3, radius = r)`
- **THEN** the generated code performs a 3D sphere-overlap search and filters matches by the listed traits

#### Scenario: Raycast query lowers to directional hit search
- **WHEN** authored code uses `query.raycast[Wall, not Trigger](origin = p, dir = d, max_dist = dist)`
- **THEN** the generated code performs a raycast-style search limited by the listed trait filters