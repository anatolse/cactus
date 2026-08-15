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
