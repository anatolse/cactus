## Purpose

Define the 3D character controller that `std.physics.volume` runs for every `CharacterBody`: what games write, what the controller writes back, and how bodies move, slide, climb, land, and separate.
## Requirements
### Requirement: The stdlib moves every 3D CharacterBody
`std.physics.volume` SHALL provide stdlib rules, members of a public group `solve` bound to `fixed_tick`, that move every entity with `CharacterBody`, `Collider`, a shape collider, and `std.transform.volume.WorldTransform` once per `fixed_tick`. A game SHALL NOT need any rule of its own for a body to fall, collide, slide, land, or climb steps. The body SHALL collide with every solid entity (one with `Solid`) whose `Collider.layer` shares a bit with the body's `Collider.mask`, whatever its shape, including other character bodies that have `Solid`. Colliders without `Solid` SHALL NOT affect the body's motion.

#### Scenario: A body moves with no game rule
- **WHEN** a body has `velocity = vec3(3.0, 0.0, 0.0)` above an open floor and no game rule touches it
- **THEN** after one `fixed_tick` its horizontal position has advanced by `3.0 * fixed_tick.dt`

#### Scenario: The body ignores masked layers
- **WHEN** a body's mask is `1` and a solid box on layer `4` lies in its path
- **THEN** the body passes through the box

#### Scenario: The body ignores triggers
- **WHEN** a box collider without `Solid` on a layer the body's mask selects lies in its path
- **THEN** the body passes through the box

#### Scenario: A body without Solid blocks nothing
- **WHEN** body A has no `Solid` and body B walks into it
- **THEN** B passes through A

#### Scenario: No body, no effect
- **WHEN** a program imports `std.physics.volume` and no entity has `CharacterBody`
- **THEN** the program's observable behavior is the same as without the controller

### Requirement: Game rules write intent before the controller runs
A game rule that writes `CharacterBody.velocity` in `fixed_tick`, with no explicit order against `solve`, SHALL take effect in the same tick. A game rule that declares `after: physics.solve` SHALL see this tick's controller results.

#### Scenario: Velocity written this tick moves the body this tick
- **WHEN** a game `fixed_tick` rule sets `velocity.x = 2.0` on a resting body
- **THEN** the same tick's movement uses `velocity.x = 2.0`

#### Scenario: Reading results after the controller
- **WHEN** a game rule declares `after: physics.solve` and reads `grounded` in `fixed_tick`
- **THEN** it sees the value the controller set in the same tick

### Requirement: Gravity applies to unsupported bodies
On each tick, a body that was not grounded at the start of the tick, or that has `velocity.y > 0`, SHALL have its `velocity.y` reduced by `gravity * fixed_tick.dt` before it moves. A body grounded at the start of the tick with `velocity.y <= 0` SHALL get no gravity that tick. `gravity = 0.0` SHALL disable it.

#### Scenario: Falling accelerates
- **WHEN** a body with `gravity = 10.0` starts in the air at rest
- **THEN** after two ticks its `velocity.y` is `-20.0 * fixed_tick.dt` and it has moved down

#### Scenario: A grounded body on a slope does not creep
- **WHEN** a grounded body with zero velocity rests on a walkable 30-degree slope
- **THEN** its position does not change over 60 ticks

### Requirement: Bodies never pass through colliders
A body SHALL NOT end a tick overlapping a solid collider it did not overlap at the start of the tick, except another moving character body. Fast motion SHALL NOT tunnel through thin solid geometry.

#### Scenario: Fast body against a thin wall
- **WHEN** a body moves at a speed that covers four wall thicknesses per tick toward a thin solid box wall
- **THEN** the body stops on the near side of the wall

### Requirement: Blocked motion slides along surfaces
When a body's motion hits a surface, the controller SHALL move it up to the contact and then continue with the remaining motion projected onto the contact plane, for up to three contacts per tick. The body's `velocity` SHALL lose its component into the surface and keep the rest. A surface the body can't stand on SHALL NOT add upward speed to motion or `velocity` that wasn't already rising.

#### Scenario: Diagonal motion slides along a wall
- **WHEN** a grounded body moves at 45 degrees into a long wall
- **THEN** it keeps moving along the wall at the tangential speed
- **AND** its `velocity` has no component into the wall

#### Scenario: An edge doesn't launch the body
- **WHEN** a grounded body walks into the top edge of a box taller than `step_height`
- **THEN** its `velocity.y` stays at or below zero

#### Scenario: Corner stops the body without jitter
- **WHEN** a body moves into the inside corner of two perpendicular walls
- **THEN** it comes to rest touching both walls
- **AND** its position does not change between later ticks

### Requirement: Grounding output
After each tick, `grounded` SHALL be true exactly when the body is resting on a walkable surface. A surface is walkable when the angle from world up of its surface normal at the contact is at most `max_slope` degrees. On a box edge or corner, the surface normal is that of the face the body's motion meets first, so a body whose rounded bottom rests on the edge of a box top stands on that top. `ground_normal` SHALL be that surface normal while grounded and `vec3(0.0, 1.0, 0.0)` otherwise. `time_since_grounded` SHALL be `0.0` while grounded and SHALL grow by `fixed_tick.dt` each tick otherwise. Landing SHALL set `velocity.y` to zero.

#### Scenario: Landing on a floor
- **WHEN** a falling body reaches a flat floor
- **THEN** it rests on the floor with `grounded = true`, `ground_normal = vec3(0.0, 1.0, 0.0)`, and `velocity.y = 0.0`

#### Scenario: Too steep to stand on
- **WHEN** a body with `max_slope = 45.0` rests against a 60-degree slope
- **THEN** `grounded` is false and the body slides down the slope

#### Scenario: Coyote time from time_since_grounded
- **WHEN** a body walks off a ledge and falls for three ticks
- **THEN** `time_since_grounded` is `3.0 * fixed_tick.dt`

### Requirement: Jumping is a positive vertical velocity
A game SHALL make a body jump by setting `velocity.y` to a positive value. A body with `velocity.y > 0` at the start of a tick SHALL NOT be held to the ground or snapped down that tick.

#### Scenario: Jump leaves the ground
- **WHEN** a grounded body gets `velocity.y = 6.0`
- **THEN** on the next tick it rises and `grounded` is false

#### Scenario: Ceiling stops the jump
- **WHEN** a jumping body hits a ceiling
- **THEN** its `velocity.y` becomes zero or negative and it falls back

### Requirement: Grounded bodies climb steps and follow descents
A grounded body moving horizontally SHALL climb onto a walkable surface whose contact is at most `step_height` above the ground it stood on, within the same tick, with no upward velocity. A grounded body walking over a drop of at most `step_height` SHALL stay on the lower surface rather than fall. A drop of more than `step_height` SHALL make it fall under gravity. A step SHALL NOT be climbed when a ceiling leaves no room to rise. A grounded body SHALL NOT climb a slope steeper than `max_slope` or a box taller than `step_height`.

#### Scenario: Climbing a step
- **WHEN** a grounded body with `step_height = 0.55` walks into a 0.5-high box step
- **THEN** it ends on top of the step, grounded, without leaving the ground for a tick

#### Scenario: Blocked by a tall step
- **WHEN** the same body walks into a 0.8-high box
- **THEN** it stops at the box and stays on the floor

#### Scenario: Walking into a steep ramp
- **WHEN** a grounded body with `max_slope = 45.0` walks into a 60-degree ramp
- **THEN** it stays at the foot of the ramp

#### Scenario: Walking down stairs
- **WHEN** a grounded body walks down a staircase of 0.5-high steps with `step_height = 0.55`
- **THEN** it is grounded at the end of every tick

#### Scenario: Walking off a high ledge
- **WHEN** a grounded body walks off a 2.0-high ledge
- **THEN** it falls over several ticks and lands below

### Requirement: Overlapping bodies separate
A body that overlaps a solid collider SHALL be moved out of it by the end of the tick. When two solid character bodies that collide with each other overlap, each SHALL take half of the correction. Overlap SHALL NOT trap a body: a body overlapping a solid collider SHALL still be able to move away from it. Overlap with a trigger SHALL cause no correction.

#### Scenario: Spawned inside a wall
- **WHEN** a body spawns overlapping a solid box wall
- **THEN** after one tick it no longer overlaps the wall

#### Scenario: Two bodies split the correction
- **WHEN** two solid character bodies on the same layer overlap by `0.2` along X and neither moves
- **THEN** after one tick each has moved `0.1` apart and they no longer overlap

#### Scenario: Bodies walking into each other
- **WHEN** two solid bodies walk straight into each other
- **THEN** they stop facing each other, and they overlap by no more than one tick's motion at any time

#### Scenario: Standing inside a trigger
- **WHEN** a body overlaps a trigger collider and does not move
- **THEN** after one tick its position is unchanged

### Requirement: Subtrees under bodies follow them in the same tick
After the `solve` group in each `fixed_tick`, `std.physics.volume` SHALL re-derive `WorldTransform` for every descendant of a character body that has `Parent`, `LocalTransform` and `WorldTransform`, using the hierarchy propagation rules. A `fixed_tick` rule that declares `after: physics.solve` SHALL see those descendants at this tick's body pose. Descendants of entities that are not character bodies SHALL NOT be propagated in `fixed_tick` by this rule.

#### Scenario: Child collider follows its body this tick
- **WHEN** a body moves `0.5` along X during `solve` and has a child with `LocalTransform` position `(0, 1, 0)`
- **THEN** a rule after `physics.solve` in the same `fixed_tick` reads the child's `WorldTransform.position` as the body's new position plus `(0, 1, 0)`

#### Scenario: Nested descendants follow
- **WHEN** a body's child has its own child, both with `LocalTransform`
- **THEN** both descendants reflect the body's pose after `solve`

#### Scenario: Non-body subtrees wait for late_tick
- **WHEN** a pose root without `CharacterBody` has a child with `LocalTransform`
- **THEN** the child is not re-derived during `fixed_tick`, and `late_tick` propagation updates it

