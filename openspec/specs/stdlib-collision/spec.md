# stdlib-collision Specification

## Purpose
Provide backend-independent, pure geometric overlap predicates that gameplay code — including `where:` clauses — can call directly, without depending on any particular collider trait or physics subsystem.

## Requirements

### Requirement: std.collision.flat provides a pure circle-overlap predicate
`std.collision.flat` SHALL export `pub func circles_overlap(a_position: vec2, a_radius: float, b_position: vec2, b_radius: float) bool`, implemented as an ordinary (non-`extern`) Cactus function whose body is the portable reference implementation: two circles overlap when the squared distance between their centers is strictly less than the square of their summed radii.

#### Scenario: Overlapping circles return true
- **WHEN** `circles_overlap` is called with two circles whose centers are closer than the sum of their radii
- **THEN** it returns `true`

#### Scenario: Touching circles return false
- **WHEN** `circles_overlap` is called with two circles exactly the sum of their radii apart
- **THEN** it returns `false`

#### Scenario: Separated circles return false
- **WHEN** `circles_overlap` is called with two circles farther apart than the sum of their radii
- **THEN** it returns `false`

### Requirement: std.collision.volume provides a pure sphere-overlap predicate
`std.collision.volume` SHALL export `pub func spheres_overlap(a_position: vec3, a_radius: float, b_position: vec3, b_radius: float) bool`, implemented as an ordinary (non-`extern`) Cactus function whose body is the portable reference implementation: two spheres overlap when the squared distance between their centers is strictly less than the square of their summed radii.

#### Scenario: Overlapping spheres return true
- **WHEN** `spheres_overlap` is called with two spheres whose centers are closer than the sum of their radii
- **THEN** it returns `true`

#### Scenario: Touching spheres return false
- **WHEN** `spheres_overlap` is called with two spheres exactly the sum of their radii apart
- **THEN** it returns `false`

### Requirement: std.collision predicates are usable inside where:
Because their bodies are ordinary pure Cactus functions, `circles_overlap` and `spheres_overlap` SHALL be legal calls inside a `where:` predicate, subject to the same purity rules as any other pure function call.

#### Scenario: spheres_overlap used as a where: predicate
- **WHEN** a pair rule's `where:` clause calls `collision.spheres_overlap(a.transform.position, a.sphere.radius, b.transform.position, b.sphere.radius)`
- **THEN** the call is accepted as a pure `where:` predicate

### Requirement: std.collision is independent of std.physics collider traits
`std.collision.flat` and `std.collision.volume` SHALL take plain `vec2`/`vec3` positions and `float` radii as arguments and SHALL NOT reference `std.physics.flat.CircleCollider`, `std.physics.volume.SphereCollider`, or any other collider trait. Callers MAY supply position and radius values sourced from any trait, including locally declared, non-stdlib collider traits.

#### Scenario: Locally declared collider trait supplies predicate arguments
- **WHEN** a program declares its own local `SphereCollider` trait (distinct from `std.physics.volume.SphereCollider`) and passes its `radius` field to `spheres_overlap`
- **THEN** the call type-checks without importing `std.physics.volume`

### Requirement: std.collision.volume provides pure sphere-box separation
std.collision.volume SHALL export pub func sphere_box_separation(sphere_position: vec3, sphere_radius: float, box_position: vec3, box_size: vec3, box_rotation: quat) vec3 as an ordinary pure Cactus function. The result SHALL be the shortest world-space translation that moves an overlapping sphere out of the oriented box, and SHALL be the zero vector when the shapes are separated or only touching. The function SHALL depend only on plain math values and SHALL NOT reference std.physics collider traits.

#### Scenario: Separated sphere returns zero
- **WHEN** the sphere lies outside the oriented box with positive clearance
- **THEN** sphere_box_separation returns vec3(0.0, 0.0, 0.0)

#### Scenario: Outside-center overlap returns outward separation
- **WHEN** the sphere center is outside the box but the sphere surface overlaps it
- **THEN** the returned vector points from the closest box point toward the sphere center
- **AND** its length equals the penetration depth

#### Scenario: Sphere center inside box uses nearest face
- **WHEN** the sphere center lies inside the box
- **THEN** the returned vector moves it through the nearest box face by the face distance plus the nonnegative sphere radius
- **AND** equal-distance face ties resolve deterministically in local X, then Y, then Z order

#### Scenario: Rotated box returns world-space result
- **WHEN** the box has a non-identity rotation
- **THEN** overlap is evaluated in box-local space
- **AND** the separation vector is rotated back into world space

#### Scenario: Separation is usable in a where clause
- **WHEN** a pure where predicate compares the returned separation with the zero vector or reads its length
- **THEN** semantic analysis accepts the call as pure
