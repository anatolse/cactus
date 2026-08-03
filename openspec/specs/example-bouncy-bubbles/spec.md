# example-bouncy-bubbles Specification

## Purpose

Define the bouncy-bubbles Cactus example: a self-contained module demonstrating cross-domain and same-domain pair rules for circle-vs-box and circle-vs-circle collision response, elastic reflection and mass-weighted collision resolution, and circle/rectangle shape rendering.

## Requirements

### Requirement: Bouncy bubbles Cactus module

The example SHALL provide a single `examples/bouncy-bubbles/main.cactus` file that is valid Cactus DSL. It SHALL declare a `Bubble` trait with `velocity: vec2` and `radius: float` fields, a `Wall` trait with a `normal: vec2` field, 4 `Wall` entities forming a static square arena boundary, and 6 `Bubble` entities with distinct `radius` values and initial `velocity`.

#### Scenario: Module compiles without errors
- **WHEN** the compiler is invoked with `cactus examples/bouncy-bubbles/main.cactus --backend cpp-entt -o bouncy-bubbles.generated.h`
- **THEN** the command exits with code 0 and produces valid generated C++ output

#### Scenario: Six bubbles with distinct radii
- **WHEN** the module's entity declarations are inspected
- **THEN** exactly 6 entities apply `Bubble`, and no two of them declare the same `radius` value

#### Scenario: Four walls form a closed boundary
- **WHEN** the module's entity declarations are inspected
- **THEN** exactly 4 entities apply `Wall`, each with a `normal` field set to one of the 4 axial directions, together enclosing the bubbles' spawn area

### Requirement: Cross-domain pair rule detects bubble-wall contact

The example SHALL declare a rule with a `pairs:` domain binding a `Bubble`-selecting binding against a `Wall`-selecting binding (two different entity sets), detecting circle-vs-box overlap and emitting a targeted bounce event to the bubble binding only when the bubble is moving toward the wall along its normal.

#### Scenario: Overlapping and approaching pair emits a targeted event
- **WHEN** a bubble's circle overlaps a wall's box on `tick` and the bubble's velocity has a positive closing component along the wall's `normal`
- **THEN** the pair handler emits a targeted bounce event to the bubble binding carrying the wall's `normal`

#### Scenario: Separating pair does not re-emit
- **WHEN** a bubble still overlaps a wall but its velocity's closing component along the wall's `normal` is zero or negative
- **THEN** the pair handler does not emit a bounce event for that tuple

#### Scenario: Pair handler does not mutate durable bubble or wall traits
- **WHEN** the wall-contact pair handler body is inspected
- **THEN** it contains no assignment or compound assignment to `Bubble` or `Wall` fields through either pair binding

### Requirement: Unary rule resolves wall bounces with reflected velocity

The example SHALL declare a unary rule, filtered on `Bubble`, that consumes the wall-bounce event and reflects the entity's `velocity` across the event's `normal`, conserving speed (elastic reflection).

#### Scenario: Velocity reflects across the wall normal
- **WHEN** the wall-bounce resolver handles an event with a given `normal` for a bubble with incoming `velocity`
- **THEN** the bubble's `velocity` is updated to the mirror of the incoming velocity about `normal`
- **AND** the magnitude of the updated velocity equals the magnitude of the incoming velocity

### Requirement: Same-domain pair rule detects bubble-bubble contact

The example SHALL declare a rule with a `pairs:` domain binding two `Bubble`-selecting bindings drawn from the same entity set, computing a mass-weighted elastic collision response and emitting a targeted bounce event carrying the resolved post-collision velocity to each participating bubble independently.

#### Scenario: Same-domain pair includes self-pairs and both directions
- **WHEN** the bubble-bubble pair rule's declared bindings are inspected
- **THEN** both bindings select the `Bubble` trait, matching the shipped `dsl-pair-relations` semantics that such a relation includes `(a,a)`, `(a,b)`, `(b,a)`, and `(b,b)` tuples for two live bubbles `a` and `b`

#### Scenario: Self-pairs and non-overlapping pairs are rejected
- **WHEN** a tuple's two bindings resolve to the same entity, or the two bubbles' circles do not overlap
- **THEN** the pair handler does not emit a bounce event for that tuple

#### Scenario: Approaching, overlapping pair emits one targeted event per side
- **WHEN** two distinct bubbles overlap and are closing (positive relative velocity along the line between their centers)
- **THEN** the tuple with binding order `(a,b)` emits a targeted bounce event to `a` carrying `a`'s resolved post-collision velocity
- **AND** the reverse tuple `(b,a)` emits a targeted bounce event to `b` carrying `b`'s resolved post-collision velocity

#### Scenario: Heavier bubble imparts more change on a lighter bubble
- **WHEN** two bubbles of different `radius` collide head-on
- **THEN** the resolved post-collision velocities are computed using mass proportional to `radius * radius` for each bubble, so the smaller bubble's velocity changes more than the larger bubble's

### Requirement: Unary rule applies resolved bubble-bubble velocity

The example SHALL declare a unary rule, filtered on `Bubble`, that consumes the bubble-bubble bounce event and assigns the event's resolved velocity directly to the entity's `velocity`.

#### Scenario: Resolved velocity is applied
- **WHEN** the bubble-bubble resolver handles a bounce event carrying a resolved velocity for a bubble
- **THEN** that bubble's `velocity` is set to the event's resolved velocity

### Requirement: Bubbles render as circles and walls render as rectangles

The example SHALL apply `std.render.shapes.Shape` with `ShapeType.Circle` to each `Bubble` entity (sized from its `radius`) and `ShapeType.Rectangle` to each `Wall` entity, so the arena and bubbles are visually distinguishable when rendered.

#### Scenario: Bubble shape reflects its radius
- **WHEN** a `Bubble` entity's `Shape` component is inspected
- **THEN** `type` is `ShapeType.Circle` and `size.x` equals twice that bubble's `radius`

#### Scenario: Wall shape is a rectangle
- **WHEN** a `Wall` entity's `Shape` component is inspected
- **THEN** `type` is `ShapeType.Rectangle`
