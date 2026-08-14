# example-bouncy-bubbles Specification

## Purpose

Define the bouncy-bubbles Cactus example: a self-contained module demonstrating cross-domain and same-domain pair rules for circle-vs-box and circle-vs-circle collision response, elastic reflection and mass-weighted collision resolution, and circle/rectangle shape rendering.

## Requirements

### Requirement: Bouncy bubbles Cactus module

The example SHALL provide a single `examples/bouncy-bubbles/main.cactus` file that is valid Cactus DSL. It SHALL declare a `Bubble` trait with only a `velocity: vec2` field, a `CircleCollider` trait with a `radius: float` field, an empty `Wall` marker trait, and a `SquareCollider` trait with a `size: vec2` field. Collider traits SHALL be independent of `std.render.shapes.Shape` and SHALL NOT duplicate their data onto `Bubble` or `Wall`. The module SHALL declare 4 `Wall` entities forming a static arena boundary and 6 `Bubble` entities with distinct `CircleCollider.radius` values and initial `velocity`.

#### Scenario: Module compiles without errors
- **WHEN** the compiler is invoked with `cactus examples/bouncy-bubbles/main.cactus --backend cpp-entt -o bouncy-bubbles.generated.h`
- **THEN** the command exits with code 0 and produces valid generated C++ output

#### Scenario: Six bubbles with distinct radii
- **WHEN** the module's entity declarations are inspected
- **THEN** exactly 6 entities apply `Bubble` and `CircleCollider`, and no two of them declare the same `CircleCollider.radius` value

#### Scenario: Four walls form a closed boundary
- **WHEN** the module's entity declarations are inspected
- **THEN** exactly 4 entities apply `Wall` and `SquareCollider`, together enclosing the bubbles' spawn area, and at least one of the 4 walls has a `WorldTransform.rotation` that is not a multiple of 90 degrees

#### Scenario: Collider data is not duplicated on gameplay traits
- **WHEN** the `Bubble` and `Wall` trait declarations are inspected
- **THEN** `Bubble` declares only `velocity` and `Wall` declares no fields — radius and size live exclusively on `CircleCollider` and `SquareCollider`

### Requirement: Wall and bubble entities are template-backed

The example SHALL declare a `template` archetype for walls and a `template` archetype for bubbles, each bundling the traits and default field values shared by every instance of that kind. Every `Wall` entity and every `Bubble` entity in the module SHALL be declared as a template-backed entity (`entity Name from <Template>:`) rather than an inline `entity Name:` declaration, overriding only the fields that differ per instance.

#### Scenario: Wall entities share one template
- **WHEN** the module's wall entity declarations are inspected
- **THEN** all 4 wall entities are declared `from` the same wall template, each overriding only its `position` and `rotation`

#### Scenario: Bubble entities share one template
- **WHEN** the module's bubble entity declarations are inspected
- **THEN** all 6 bubble entities are declared `from` the same bubble template, each overriding only the fields that vary per bubble (at minimum `position`, `velocity`, and `CircleCollider.radius`)

### Requirement: Cross-domain pair rule detects bubble-wall contact

The example SHALL declare a rule with a `pairs:` domain binding a `Bubble`/`CircleCollider`-selecting binding against a `Wall`/`SquareCollider`-selecting binding (two different entity sets), detecting circle-vs-oriented-rectangle overlap using the wall's current `WorldTransform.rotation` (not assuming world-axis alignment), and emitting a targeted bounce event to the bubble binding only when the bubble is moving toward the wall along the wall's current contact normal.

#### Scenario: Overlapping and approaching pair emits a targeted event
- **WHEN** a bubble's circle overlaps a wall's oriented rectangle on `tick` and the bubble's velocity has a positive closing component along the wall's current contact normal
- **THEN** the pair handler emits a targeted bounce event to the bubble binding carrying the wall's current contact normal

#### Scenario: Separating pair does not re-emit
- **WHEN** a bubble still overlaps a wall but its velocity's closing component along the wall's current contact normal is zero or negative
- **THEN** the pair handler does not emit a bounce event for that tuple

#### Scenario: Detection is correct regardless of wall rotation
- **WHEN** a wall's `WorldTransform.rotation` is not a multiple of 90 degrees
- **THEN** the pair handler still correctly detects circle-vs-rectangle overlap by evaluating the bubble's position in the wall's rotated local space, not world-axis-aligned space

#### Scenario: Pair handler does not mutate durable bubble or wall traits
- **WHEN** the wall-contact pair handler body is inspected
- **THEN** it contains no assignment or compound assignment to `Bubble`, `Wall`, `CircleCollider`, or `SquareCollider` fields through either pair binding

### Requirement: Wall contact normal is derived from orientation

The `Wall` trait SHALL NOT store an authored normal field. Each wall's outward contact normal SHALL be computed by rotating a fixed local-space outward direction by the wall's current `WorldTransform.rotation`, so the effective normal always matches the wall's current orientation with no separately-authored value that can drift out of sync.

#### Scenario: Derived normal matches an axis-aligned wall
- **WHEN** a wall's `WorldTransform.rotation` is `0`, `90`, `180`, or `270` degrees
- **THEN** its derived contact normal points along the corresponding world axis, matching what an equivalent hand-authored axial normal would have been

#### Scenario: Derived normal follows a non-axial wall
- **WHEN** a wall's `WorldTransform.rotation` is not a multiple of 90 degrees
- **THEN** its derived contact normal is the corresponding rotation of the local outward direction, and is neither purely horizontal nor purely vertical

### Requirement: Unary rule resolves wall bounces with reflected velocity

The example SHALL declare a unary rule, filtered on `Bubble`, that consumes the wall-bounce event and reflects the entity's `velocity` across the event's `normal`, conserving speed (elastic reflection).

#### Scenario: Velocity reflects across the wall normal
- **WHEN** the wall-bounce resolver handles an event with a given `normal` for a bubble with incoming `velocity`
- **THEN** the bubble's `velocity` is updated to the mirror of the incoming velocity about `normal`
- **AND** the magnitude of the updated velocity equals the magnitude of the incoming velocity

### Requirement: Same-domain pair rule detects bubble-bubble contact

The example SHALL declare a rule with a `pairs:` domain binding two `Bubble`/`CircleCollider`-selecting bindings drawn from the same entity set, computing a mass-weighted elastic collision response and emitting a targeted bounce event carrying the resolved post-collision velocity to each participating bubble independently.

#### Scenario: Same-domain pair includes self-pairs and both directions
- **WHEN** the bubble-bubble pair rule's declared bindings are inspected
- **THEN** both bindings select the `Bubble` and `CircleCollider` traits, matching the shipped `dsl-pair-relations` semantics that such a relation includes `(a,a)`, `(a,b)`, `(b,a)`, and `(b,b)` tuples for two live bubbles `a` and `b`

#### Scenario: Self-pairs and non-overlapping pairs are rejected
- **WHEN** a tuple's two bindings resolve to the same entity, or the two bubbles' circles do not overlap
- **THEN** the pair handler does not emit a bounce event for that tuple

#### Scenario: Approaching, overlapping pair emits one targeted event per side
- **WHEN** two distinct bubbles overlap and are closing (positive relative velocity along the line between their centers)
- **THEN** the tuple with binding order `(a,b)` emits a targeted bounce event to `a` carrying `a`'s resolved post-collision velocity
- **AND** the reverse tuple `(b,a)` emits a targeted bounce event to `b` carrying `b`'s resolved post-collision velocity

#### Scenario: Heavier bubble imparts more change on a lighter bubble
- **WHEN** two bubbles of different `CircleCollider.radius` collide head-on
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
