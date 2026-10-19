# stdlib-noise Specification

## Purpose
Provide reusable, seeded spatial noise for procedural worlds and other Cactus simulations without mutable random-generator state.

## Requirements
### Requirement: Pure seeded Perlin samples
std.noise SHALL provide perlin2(point: vec2, seed: int) and perlin3(point: vec3, seed: int) returning normalized floats in [-1, 1]. Valid samples SHALL be continuous across lattice boundaries. Sampling SHALL depend only on the coordinates, seed and documented algorithm version, with no world reads, effects or RNG advancement. Supported coordinates SHALL be finite and within [-1000000, 1000000] on every axis; unsupported input SHALL return zero.

#### Scenario: Sampling order does not change terrain
- **WHEN** the same valid positions and seed are sampled in different orders
- **THEN** each position yields the same result within the same implementation
- **AND** std.random generator state is unchanged

#### Scenario: Invalid input is total
- **WHEN** a coordinate is NaN, infinite or outside the supported domain
- **THEN** the Perlin result is zero without a crash or unbounded work

### Requirement: Voronoi samples expose region identity and distances
std.noise SHALL provide voronoi2 and voronoi3 with the same input domains and seed semantics. A valid result SHALL identify the nearest feature's integer lattice cell and provide Euclidean distances to the nearest and second-nearest features, with 0 <= first <= second. Distances SHALL NOT be artificially normalized to [0, 1]. Equal distances SHALL break ties lexicographically by feature-cell coordinate. Invalid input SHALL yield valid=false, zero distances and zero coordinates. A hash value SHALL NOT be documented as a unique region identity.

#### Scenario: Region borders are repeatable
- **WHEN** a point is equidistant from two generated feature points
- **THEN** the documented coordinate tie-break selects the same region independent of sample order

#### Scenario: Second nearest feature is correctly reported
- **WHEN** a valid point is sampled
- **THEN** both distances and the winning cell agree with a reference search covering every possible nearer candidate

### Requirement: Noise algorithms have a reproducibility contract
The module SHALL document seed hashing, feature placement, gradients, coordinate bounds, tie-breaking, normalization and algorithm version, and SHALL include reference samples. Supported compilers SHALL agree on integer hashing and lattice decisions; floating comparisons SHALL use a documented tolerance. No universal bit-identical floating-point guarantee is implied.

#### Scenario: Compiler conformance
- **WHEN** reference samples run with supported MSVC and Clang builds
- **THEN** lattice identities and hashes match and numeric samples satisfy the documented tolerance

### Requirement: Noise algorithms are authored in Cactus stdlib
Perlin interpolation/gradient selection and Voronoi candidate evaluation SHALL be implemented as ordinary pure Cactus funcs using finite loops, local values and reusable math helpers. Existing proven-pure math/random primitives MAY be reused. The initial implementation SHALL NOT require Perlin-specific or Voronoi-specific extern bindings. Terrain composition SHALL remain example logic. Later native specialization requires measured evidence and equivalence to the authored reference.

#### Scenario: Noise works through ordinary function compilation
- **WHEN** a program imports std.noise and samples either algorithm
- **THEN** the program uses compiled Cactus algorithm bodies without a dedicated native noise implementation

#### Scenario: Candidate search remains bounded
- **WHEN** Voronoi evaluates a point
- **THEN** its finite candidate domain includes both nearest features under the documented placement bounds
- **AND** it performs no open-ended search or world queries
