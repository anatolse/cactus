# dsl-drawing-rules Specification

## Purpose

Define drawing rules: unary rules in the `render` phase whose vertex and fragment stage handlers are written in Cactus, read like an ordinary rule, and draw one quad per row at the rule's place among the render layers.

## Requirements

### Requirement: Stage triggers of the render phase
`std.core.render` SHALL expose two derived triggers, `render.vertex` and `render.fragment`, used with `on <trigger> as <alias>:`. The alias SHALL be required. The triggers SHALL be recognized by the phase's canonical identity, so an aliased import works the same. No other phase SHALL expose them, and a phase SHALL NOT become a render pass through any field type.

#### Scenario: Triggers resolve on render
- **WHEN** a rule declares `on render.vertex as v:` and `on render.fragment as f:`
- **THEN** both resolve to the stage triggers of `std.core.render`

#### Scenario: Other phase
- **WHEN** a rule declares `on tick.vertex as v:`
- **THEN** the compiler reports an error saying `tick` has no such stage

### Requirement: A drawing rule has both stage handlers and a unary domain
A rule with a handler on `render.vertex` or `render.fragment` SHALL be a drawing rule. A drawing rule MUST have exactly one handler on each of the two triggers and no other handler. It MUST declare `each:` and MAY declare `without:`, `where:`, `order by:`, `group:`, `after:` and `before:` with their ordinary meaning. `pairs:`, `reduce:`, `limit:`, `repeat:` and `keep` SHALL each be rejected with a source-located error. A program MAY contain any number of drawing rules.

#### Scenario: Missing stage
- **WHEN** a rule has a `render.vertex` handler and no `render.fragment` handler
- **THEN** the compiler reports an error naming the rule and the missing stage

#### Scenario: Pair domain
- **WHEN** a drawing rule declares `pairs:`
- **THEN** the compiler reports an error saying a drawing rule needs a unary `each:`

#### Scenario: Repeat rejected
- **WHEN** a drawing rule declares `repeat: 2`
- **THEN** the compiler reports an error at the clause saying a drawing rule draws each row once

#### Scenario: Several drawing rules
- **WHEN** a program declares two drawing rules with different domains
- **THEN** both compile and each draws its own rows

### Requirement: A drawing rule draws exactly its domain
A drawing rule SHALL draw one quad for each entity of its domain and for no other entity. The domain SHALL be the one an ordinary unary rule with the same clauses has: the entity carries every `each:` trait, carries no `without:` trait, and passes `where:`. Quads SHALL be drawn in `order by:` order when given, else in creation order.

#### Scenario: Entity with only the first trait
- **WHEN** a drawing rule declares `each: WorldTransform as xf, Particle as particle` and an entity carries `WorldTransform` but not `Particle`
- **THEN** no quad is drawn for it

#### Scenario: Excluded trait
- **WHEN** a drawing rule declares `without: Hidden` and a particle carries `Hidden`
- **THEN** no quad is drawn for it

#### Scenario: Value filter
- **WHEN** a drawing rule declares `where: particle.age < 1.0` and a particle's age is 2.0
- **THEN** no quad is drawn for it

### Requirement: Stage handlers read like a unary rule
Both stage handlers of a drawing rule SHALL be able to read: the fields of the rule's `each:` traits through their aliases; fields of named entities; fields reached through a link of the rule's row; constants. The same read SHALL be accepted or rejected as it would be in an ordinary handler of a rule with the same domain. A value read from the row, a link or a named entity SHALL be the same for every vertex and every sample of that row's quad.

#### Scenario: Fragment reads a row field
- **WHEN** the fragment handler of a rule with `each: Particle as particle` reads `particle.age`
- **THEN** every sample of a particle's quad sees that particle's age

#### Scenario: Named entity read
- **WHEN** a vertex handler reads `vp.Screen[vp.ScreenSize].size`
- **THEN** the read is accepted and every row sees the current screen size

#### Scenario: Link read
- **WHEN** a rule over `each: Beam as beam` reads `beam.sink[tf.WorldTransform].position` in its vertex handler
- **THEN** each beam's quad uses the position of its own sink entity

### Requirement: Value types a stage reads
A stage read whose type is not `int`, `float`, `bool`, `vec2`, `vec3`, `color` or an enum SHALL be an error naming the type; for `duration` it SHALL suggest `seconds(...)` (`dsl-duration`). An aliased dimensioned trait in `each:` and a cell access SHALL be errors (`dsl-trait-dimensions`).

#### Scenario: Unsupported type
- **WHEN** a stage handler reads a `string` field
- **THEN** the compiler reports an error naming `string`

#### Scenario: Integer and vector reads
- **WHEN** a vertex handler reads an `int` field, a `vec3` field and an enum field of its row
- **THEN** compilation succeeds and each quad sees its own row's values

### Requirement: Stage handlers write only stage outputs
A stage handler SHALL NOT assign a trait field, issue `spawn`, `destroy`, `add`, `remove`, `set`, `project` or `emit`, run a world query, or call an extern function that acts. Its only writable names SHALL be its locals and the stage's output fields.

#### Scenario: Write rejected
- **WHEN** a stage handler assigns `particle.age = 0.0` or contains `destroy`
- **THEN** the compiler reports an error saying a stage handler writes only stage outputs

#### Scenario: World query rejected
- **WHEN** a stage handler calls `query.first[Player]()`
- **THEN** the compiler reports an error naming the query

### Requirement: Stage built-in fields
The vertex alias SHALL expose the inputs `corner: vec2`, `uv: vec2` and `vertex_index: int` (the quad's corners (-1,-1), (1,-1), (1,1) and (-1,1), with UVs (0,0), (1,0), (1,1) and (0,1); the order in which vertices run is the backend's) and the outputs `world_position: vec2`, `screen_position: vec2`, `uv_out: vec2` and `tint_out: color`. The fragment alias SHALL expose the inputs `uv: vec2` and `tint: color`, interpolated from `uv_out` and `tint_out`, and `frag_coord: vec2`, and the output `frag_color: color`.

#### Scenario: Corner inputs
- **WHEN** a vertex handler reads `v.corner` and `v.uv`
- **THEN** each corner of a quad sees its own corner and the UV paired with it above

### Requirement: Stage outputs and defaults
The vertex handler MUST assign exactly one of `world_position` and `screen_position` on every path; assigning both, or neither on some path, SHALL be an error. `uv_out` SHALL default to `uv` and `tint_out` to `#FFFFFFFF` when not assigned. The fragment handler MUST assign `frag_color` on every path. `frag_color` SHALL be blended over what is already drawn using its alpha.

#### Scenario: World space
- **WHEN** a vertex handler assigns `v.world_position`
- **THEN** the quad is placed through the flat camera of the viewport being drawn, as the 2D renderers place their output

#### Scenario: Screen space
- **WHEN** a vertex handler assigns `v.screen_position`
- **THEN** the quad is placed in pixels inside the viewport's rectangle, with its origin at the top-left, and a camera move does not move it

#### Scenario: Both outputs
- **WHEN** a vertex handler assigns both `world_position` and `screen_position`
- **THEN** the compiler reports an error saying to assign exactly one

### Requirement: Stage colors are unclamped
Colors SHALL stay unclamped floats inside both stages, `tint_out` and `tint` included. Only the write of `frag_color` to the 8-bit target SHALL clamp them.

#### Scenario: Bright tint reaches the fragment
- **WHEN** a vertex handler assigns `v.tint_out = color(2.0, 0.5, 0.5, 1.0)` and the fragment handler assigns `f.frag_color = color(f.tint.r * 0.5, f.tint.g, f.tint.b, 1.0)`
- **THEN** the drawn red channel is 255, not 128

### Requirement: Drawing rules take their place among the render layers
A drawing rule SHALL be scheduled as a rule of `core.render`. Its quads SHALL be drawn after every render rule ordered before it and before every render rule ordered after it, including the stdlib renderers of the `std.render.layers` groups. It SHALL be drawn once in each active viewport. Rows SHALL render independently: one row's values SHALL NOT be visible in another row's quad.

#### Scenario: Under the HUD
- **WHEN** a drawing rule declares `group: layers.sprites` and a `ScreenLabel` overlaps one of its quads
- **THEN** the label is drawn over the quad

#### Scenario: Over everything
- **WHEN** a drawing rule is in a group ordered after every `std.render.layers` group
- **THEN** its quads are drawn over the UI

#### Scenario: Independent rows
- **WHEN** a drawing rule's domain holds several entities with different positions
- **THEN** each quad is drawn at its own entity's position

### Requirement: Stage handler body subset
A stage handler body SHALL accept `let` and `var` locals, assignment and compound assignment to locals and stage outputs, `if`/`else`, value `match` as a statement and as an expression, the `if` expression, and calls to a `func` or to an extern function that has a portable shader translation. Operators and operand types SHALL be those of an ordinary handler body. Bounded `for`, trait `match` and `return` SHALL be rejected with a diagnostic naming the statement.

#### Scenario: Vector arithmetic
- **WHEN** a vertex handler writes `v.world_position = xf.position + v.corner * half` with `vec2`, `vec2` and `float` operands
- **THEN** it type-checks exactly as in an ordinary handler

#### Scenario: Forbidden statement
- **WHEN** a stage handler contains a bounded `for`
- **THEN** the compiler reports an error naming the statement and the stage restriction
