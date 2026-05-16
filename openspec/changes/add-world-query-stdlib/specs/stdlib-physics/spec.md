## ADDED Requirements

### Requirement: std.physics.flat exposes trait-filtered query namespace
The `std.physics.flat` stdlib surface SHALL expose a `query` namespace containing 2D spatial query expressions. These queries SHALL support bracketed trait filters and named geometry arguments.

#### Scenario: Flat query namespace is available
- **WHEN** authored code imports `std.physics.flat.query`
- **THEN** 2D spatial queries such as `nearest` and `overlap_box` are available in expression position

#### Scenario: Flat nearest query supports trait filters
- **WHEN** authored code uses `query.nearest[Transform, Enemy](from = p)`
- **THEN** the query result is filtered by both 2D spatial proximity and the listed traits

#### Scenario: Flat circle overlap query is available
- **WHEN** authored code uses `query.overlap_circle[Pickup](center = p, radius = 24.0)`
- **THEN** the query performs a 2D circle-overlap search filtered by the listed traits

#### Scenario: Flat raycast query is available
- **WHEN** authored code uses `query.raycast[Wall](origin = p, dir = d, max_dist = 100.0)`
- **THEN** the query performs a 2D raycast filtered by the listed traits

### Requirement: std.physics.volume exposes trait-filtered query namespace
The `std.physics.volume` stdlib surface SHALL expose a `query` namespace containing 3D spatial query expressions. These queries SHALL mirror the flat namespace shape while using 3D spatial values.

#### Scenario: Volume query namespace is available
- **WHEN** authored code imports `std.physics.volume.query`
- **THEN** 3D spatial queries are available in expression position

#### Scenario: Volume nearest query uses 3D input
- **WHEN** authored code uses `query.nearest[Transform, Enemy](from = p3)` from the volume namespace
- **THEN** the query interprets `from` as a 3D spatial point and matches only entities satisfying the listed trait filters

#### Scenario: Volume sphere overlap query is available
- **WHEN** authored code uses `query.overlap_sphere[Pickup](center = p3, radius = 2.0)`
- **THEN** the query performs a 3D sphere-overlap search filtered by the listed traits

#### Scenario: Volume raycast query is available
- **WHEN** authored code uses `query.raycast[Wall](origin = p3, dir = d3, max_dist = 100.0)`
- **THEN** the query performs a 3D raycast filtered by the listed traits