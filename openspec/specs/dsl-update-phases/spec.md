## Requirements

### Requirement: Four-phase per-frame update model
The standard library SHALL declare runtime-rooted phases in the invariant order input -> fixed_tick -> tick -> late_tick -> render. Systems SHALL participate by declaring handlers for those phase symbols. Input, tick, late_tick, and render SHALL activate once per frame; fixed_tick SHALL activate zero or more times under its declared accumulator cadence and catch-up limit.

#### Scenario: Input precedes physics
- **WHEN** one runtime frame occurrence is injected
- **THEN** all input handlers complete before any fixed_tick repetition begins

#### Scenario: Render follows late update
- **WHEN** a frame reaches late_tick completion
- **THEN** render activates once after late_tick and receives the declared interpolation alpha

#### Scenario: Fixed tick can repeat
- **WHEN** accumulated frame time contains multiple fixed intervals
- **THEN** fixed_tick activates once per due interval up to its declared max before tick begins

### Requirement: `on input` handler is parameter-free
The `on input:` handler SHALL use the parameter-free lifecycle-handler form. Input-phase data is accessed through the lifecycle event binding (`input`) or an explicit handler alias.

#### Scenario: on input handler uses parameter-free form
- **WHEN** `on input:` appears in a system body
- **THEN** the handler is accepted without a parameter list

#### Scenario: input handler data uses lifecycle binding
- **WHEN** a handler body contains `input.pressed(Jump)`
- **THEN** the language treats `input` as the in-scope lifecycle event binding for that handler

### Requirement: `fixed_tick`, `tick`, and `late_tick` expose lifecycle data through event bindings
The `on fixed_tick:`, `on tick:`, and `on late_tick:` handlers SHALL use the parameter-free handler form. Phase-specific delta time is accessed through the lifecycle event binding (`fixed_tick.dt`, `tick.dt`, `late_tick.dt`) or through an explicit alias declared with `on <phase> as <alias>:`.

#### Scenario: tick delta time accessed through lifecycle binding
- **WHEN** a handler body contains `pos.x = pos.x + vel.x * tick.dt`
- **THEN** the language resolves `tick.dt` as the update-phase delta time

#### Scenario: fixed_tick delta time accessed through alias
- **WHEN** a system declares `on fixed_tick as ft:` and the body contains `vel.y = vel.y + gravity * ft.dt`
- **THEN** the language resolves `ft.dt` as the fixed-step delta time

#### Scenario: late_tick remains the post-update phase
- **WHEN** a system declares `on late_tick:`
- **THEN** its handler runs after all `on tick:` handlers in the same frame

### Requirement: Handler filter applies to all phases within a system
The `filter:` and `exclude:` clauses of a `system` declaration SHALL apply uniformly to all handlers within that system. A system cannot have different filters for different phases.

#### Scenario: Single filter applies across tick and late_tick
- **WHEN** a system declares `filter: Transform, FollowCamera` and both `on tick:` and `on late_tick:`
- **THEN** both handlers operate on entities that have both `Transform` and `FollowCamera` traits

