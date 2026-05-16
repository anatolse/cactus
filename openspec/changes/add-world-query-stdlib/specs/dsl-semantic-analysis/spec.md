## ADDED Requirements

### Requirement: Semantic analyzer validates query trait filters
The semantic analyzer SHALL validate that every trait referenced in a query-filter bracket resolves to a declared trait in scope using the same local/imported symbol rules as other trait-aware language constructs.

The semantic analyzer SHALL distinguish a query expression from a plain extern-function call by recognizing a resolved extern-function callee used with attached query-filter bracket metadata in a recognized query namespace.

#### Scenario: Query function resolved through imported module path
- **WHEN** authored code imports `std.query` and uses `std.query.count[EnemyAI]()`
- **THEN** the semantic analyzer resolves the callee as a query function from the imported module namespace rather than requiring a built-in keyword form

#### Scenario: Plain extern function without query filter remains ordinary call
- **WHEN** authored code uses `std.math.sqrt(x)`
- **THEN** the semantic analyzer treats it as a normal extern-function call because no query-filter metadata is attached

#### Scenario: Extern function with query filter in recognized namespace becomes query expression
- **WHEN** authored code uses `std.query.first[Boss]()`
- **THEN** the semantic analyzer treats it as a recognized query expression because the resolved callee is in a query namespace and the call carries query-filter metadata

#### Scenario: Valid query filter traits resolve successfully
- **WHEN** authored code uses `query.count[EnemyAI, not Dead]()` and both `EnemyAI` and `Dead` are declared or imported traits
- **THEN** the semantic analyzer accepts the query filter

#### Scenario: Unknown query filter trait is rejected
- **WHEN** authored code uses `query.first[GhostBoss]()` and `GhostBoss` is not a declared or imported trait
- **THEN** the semantic analyzer reports an error for the undeclared trait in the query filter

### Requirement: Semantic analyzer assigns query return types
The semantic analyzer SHALL assign fixed return types to recognized query expressions: `exists` returns `bool`, `count` returns `int`, `first`, `nearest`, and `parent` return `entity_id`, and `all` / overlap-style plural queries return `list[entity_id]`.

#### Scenario: First query inferred as entity_id
- **WHEN** authored code binds `let target = query.first[Boss]()`
- **THEN** the semantic analyzer infers `target` as type `entity_id`

#### Scenario: All query inferred as list of entity ids
- **WHEN** authored code binds `let hits = query.all[Enemy]()`
- **THEN** the semantic analyzer infers `hits` as type `list[entity_id]`

#### Scenario: Parent query inferred as entity_id
- **WHEN** authored code binds `let owner = query.parent(of = child_id)`
- **THEN** the semantic analyzer infers `owner` as type `entity_id`

### Requirement: Query expressions require world-aware context
Query expressions SHALL be treated as world-access operations. They MUST be accepted inside system event handlers and rejected inside pure `func` bodies.

#### Scenario: Query inside system handler accepted
- **WHEN** authored code evaluates `query.exists[Boss]()` inside `on tick:`
- **THEN** the semantic analyzer accepts the expression

#### Scenario: Query inside pure func rejected
- **WHEN** authored code evaluates `query.count[EnemyAI]()` inside a non-extern `func`
- **THEN** the semantic analyzer reports that query expressions require world access and are not allowed in pure functions

### Requirement: Semantic analyzer validates named arguments for recognized queries
The semantic analyzer SHALL validate the required named arguments for recognized spatial query operations and SHALL type-check each provided argument.

#### Scenario: Nearest query requires from argument
- **WHEN** authored code uses `query.nearest[Transform, Enemy]()` with no `from =` argument
- **THEN** the semantic analyzer reports that `nearest` requires a `from` argument

#### Scenario: Overlap box validates center and size argument types
- **WHEN** authored code uses `query.overlap_box[Pickup](center = p, size = s)` in `std.physics.flat.query`
- **THEN** the semantic analyzer validates `center` and `size` against the expected 2D spatial types

#### Scenario: Overlap circle validates center and radius argument types
- **WHEN** authored code uses `query.overlap_circle[Pickup](center = p, radius = r)` in `std.physics.flat.query`
- **THEN** the semantic analyzer validates `center` as a 2D point and `radius` as `float`

#### Scenario: Raycast validates origin direction and distance arguments
- **WHEN** authored code uses `query.raycast[Wall](origin = p, dir = d, max_dist = dist)`
- **THEN** the semantic analyzer validates the spatial argument set required by the active flat or volume query namespace

#### Scenario: Parent query validates entity argument
- **WHEN** authored code uses `query.parent(of = some_expr)`
- **THEN** the semantic analyzer validates that `some_expr` has type `entity_id`