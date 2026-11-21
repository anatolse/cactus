## Purpose

Define optional 2D buffered jumping with coyote time: when a jump request is accepted, how long it waits, and how often a body may jump, with exact time windows and no movement code in the controller.

## Requirements

### Requirement: Optional 2D jump controller

`std.character.flat` SHALL provide `JumpSettings` with `speed: float = 420.0`, `coyote_time: duration = 100ms` and `input_buffer: duration = 120ms`, the trait `JumpRequest` with `age: duration`, the trait `CoyoteWindow` with `airborne: duration`, the event `RequestJump` and the targeted event `Jumped`. The controller SHALL act only on entities with `JumpSettings` and `std.physics.flat.CharacterBody`. A negative window SHALL be used as zero.

#### Scenario: Opt-in only
- **WHEN** a `CharacterBody` has no `JumpSettings`
- **THEN** no controller rule selects it and its fields are not written by the controller

### Requirement: A jump writes only velocity and grounded

A jump SHALL set `velocity.y` to `-speed`, keep `velocity.x`, set `grounded` to false and emit `Jumped` to the entity. An entity SHALL carry `JumpRequest` exactly while a request waits and `CoyoteWindow` exactly while it may jump in the air, so neither is given at start. The controller SHALL NOT move the body or apply gravity.

#### Scenario: Accepted jump
- **WHEN** a grounded controlled entity has a waiting request at a fixed step
- **THEN** `velocity.y` becomes `-speed`, `velocity.x` is unchanged, and `Jumped` is delivered to that entity once

### Requirement: Buffered single-use requests

A delivered `RequestJump` SHALL add `JumpRequest` with age zero; another request while one waits SHALL add it again, which sets the age back to zero. An accepted request SHALL be removed and used once. The controller SHALL NOT create a request; holding a button makes requests only when authored code emits them.

#### Scenario: No fixed step yet
- **WHEN** a request arrives on a frame that runs no fixed step
- **THEN** it is still waiting at the next fixed step

#### Scenario: Catch-up
- **WHEN** one request is followed by several fixed steps in one frame
- **THEN** it causes at most one jump

### Requirement: Request aging

At each fixed step the controller SHALL first test the waiting request, and only when it is not accepted SHALL add the step's `delta` to its age and remove it when the age is greater than `input_buffer`. So a request SHALL be tried on the first fixed step after it even when `input_buffer` is zero, and on every step whose start is within `input_buffer` of the first.

#### Scenario: Buffered landing
- **WHEN** an airborne entity requests a jump and the game marks it grounded two steps later, with `input_buffer = 120ms` at 60 Hz
- **THEN** the controller jumps at the first step that sees it grounded

#### Scenario: Expired request
- **WHEN** an airborne entity with no `CoyoteWindow` requests a jump with `input_buffer = 100ms` at 60 Hz and is not grounded during the next 7 steps
- **THEN** the request is removed, and a later landing does not jump

#### Scenario: Zero buffer
- **WHEN** a grounded entity with `input_buffer = 0s` receives a request
- **THEN** it jumps at the next fixed step

#### Scenario: Repeated request
- **WHEN** an airborne entity requests a jump, requests again 5 steps later, and is grounded 4 steps after the second request, with `input_buffer = 100ms` at 60 Hz
- **THEN** it jumps at the first step that sees it grounded

### Requirement: Coyote window

The controller SHALL add `CoyoteWindow` with `airborne` zero at a fixed step that sees the entity grounded and not jumping. While the entity carries it, `airborne` SHALL be zero at a grounded step and grow by each other step's `delta`, and the window SHALL be removed when `airborne` is greater than `coyote_time`.

#### Scenario: Walk off an edge
- **WHEN** an entity leaves the ground without jumping and requests a jump 3 steps later, with `coyote_time = 100ms` at 60 Hz
- **THEN** one jump is accepted

#### Scenario: Last step of the window
- **WHEN** an entity leaves the ground without jumping and its request is tested 7 steps later, with `coyote_time = 100ms` at 60 Hz
- **THEN** one jump is accepted

#### Scenario: Window elapsed
- **WHEN** an entity requests a jump 8 steps after leaving the ground, with `coyote_time = 100ms` at 60 Hz
- **THEN** no jump is accepted until it is grounded while a request waits

### Requirement: Coyote eligibility

A waiting request SHALL be accepted when the entity is grounded or carries `CoyoteWindow`. An accepted jump SHALL remove `CoyoteWindow`. So after a jump no second jump SHALL be accepted until the entity has been grounded again, and an entity that has never been grounded SHALL jump only once grounded.

#### Scenario: No repeated coyote jump
- **WHEN** a second request arrives after a jump and before the entity is grounded again
- **THEN** it causes no jump in the air

#### Scenario: Spawned in the air
- **WHEN** an entity is spawned airborne with `JumpSettings` and requests a jump before it is ever grounded
- **THEN** no jump is accepted until it is grounded

### Requirement: Windows count game time

Both windows SHALL grow only by the `delta` of `core.fixed_tick` steps, so they follow the game's rate. While game time is stopped, a waiting request and an open window SHALL stay as they are.

#### Scenario: Stopped game time
- **WHEN** a grounded entity requests a jump while `Clock[Pace].scale` is `0.0`, and the scale returns to `1.0` several frames later
- **THEN** no jump happens while the scale is `0.0`, and the entity jumps at the first fixed step after it returns

#### Scenario: Slow motion
- **WHEN** `Clock[Pace].scale` is `0.5` and an entity requests a jump in the air with `input_buffer = 100ms`
- **THEN** the request is tried on the same 7 fixed steps as at scale `1.0`, which span twice as many frames

### Requirement: The game orders its movement after the controller

The controller's rules SHALL run on `core.fixed_tick` in a public rule group `character.jump`. A rule ordered after that group in the same step SHALL read the jump's `velocity`. The controller SHALL read the `grounded` value left by the previous step. The stdlib SHALL NOT integrate the body.

#### Scenario: Jump before movement
- **WHEN** a movement rule declares `after: character.jump` and a jump is accepted in a step
- **THEN** that rule moves the body with the jump velocity in the same step, and the body is moved once

#### Scenario: Grounded comes from the previous step
- **WHEN** the movement rule set `grounded = true` at the end of step n
- **THEN** the controller treats the entity as grounded in step n+1
