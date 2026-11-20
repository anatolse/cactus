# phase-activation-model Specification

## Purpose
Define first-class `phase` declarations, fixed-step accumulation and catch-up, phase-derived data, activation cardinality, and the commit boundary that bounds each phase activation.

## Requirements

### Requirement: First-class phase declarations
Cactus SHALL accept module-scope `phase Name:` declarations containing a non-empty `from:` or `after:` dependency block, optional `every:` and `max:` cadence clauses, and zero or more typed fields initialized from upstream activation data. Phase dependencies SHALL resolve to external events or phases as appropriate and SHALL form an acyclic graph.

#### Scenario: Runtime-rooted phase resolves
- **WHEN** `phase input` declares `from: frame`
- **THEN** each injected `frame` occurrence makes one input-phase activation eligible

#### Scenario: Derived phase field resolves
- **WHEN** `phase tick` declares `after: fixed_tick` and `dt: float = frame.dt`
- **THEN** `tick.dt` is initialized from the current root frame occurrence

#### Scenario: Phase cycle is rejected
- **WHEN** phase A is after phase B and phase B is transitively after phase A
- **THEN** semantic analysis reports the phase cycle

### Requirement: Once-per-source phase activation
A phase without `every:` SHALL activate exactly once for each completed activation of its source dependency set. An `after:` dependency on another phase SHALL be a completion barrier and SHALL wait for all repetitions of that upstream phase.

#### Scenario: Non-periodic phase activates once
- **WHEN** one frame occurrence activates input and input has no `every:` clause
- **THEN** input activates exactly once for that frame

#### Scenario: Downstream waits for fixed-step batch
- **WHEN** tick is after fixed_tick and the current frame causes three fixed_tick repetitions
- **THEN** tick activates once only after all three repetitions and their commits complete

#### Scenario: Downstream proceeds after zero repetitions
- **WHEN** tick is after fixed_tick and the accumulator is below the fixed interval
- **THEN** tick activates once after the empty fixed_tick batch completes

### Requirement: Periodic phase accumulation and catch-up
A phase with `every: interval` SHALL maintain an accumulator advanced by the root runtime frame's `dt`, activate once for each complete interval, and expose synthesized `dt` equal to `interval` in seconds and synthesized `delta` equal to `interval` as a `duration`. `interval` MUST be a positive compile-time `float` or `duration`. With a `duration` interval, `dt` SHALL be `seconds(interval)`; with a `float` interval, `delta` SHALL be `duration(interval)`. `max` MUST be a positive compile-time integer and SHALL cap repetitions per root activation.

If more complete intervals are due than `max`, the scheduler SHALL execute `max` repetitions, drop the remaining unexecuted whole intervals, and preserve only the fractional remainder. This policy SHALL keep synthesized `alpha` in `[0.0, 1.0)`.

#### Scenario: Multiple fixed repetitions run
- **WHEN** the accumulator contains at least three intervals and `max` is at least three
- **THEN** the phase activates three times and each activation observes `phase.dt == interval`

#### Scenario: Catch-up cap drops excess whole steps
- **WHEN** ten intervals are due and `max: 8`
- **THEN** eight repetitions run, two unexecuted whole intervals are dropped, and the fractional remainder is preserved

#### Scenario: A duration interval
- **WHEN** a phase declares `every: 1s / 60`
- **THEN** each activation observes `phase.delta` of 11,760,000 units and `phase.dt` equal to the binary32 value nearest 1/60

#### Scenario: Invalid cadence is rejected
- **WHEN** `every` is non-constant or non-positive, or `max` is non-constant or non-positive
- **THEN** semantic analysis reports an invalid phase cadence

### Requirement: Periodic phase completion alpha
After a periodic phase's repetition batch, the phase SHALL expose synthesized completion field `alpha = remainder / interval` to downstream phase initializers. `alpha` SHALL be a batch result and MUST NOT be read by handlers executing the periodic repetitions.

#### Scenario: Render receives interpolation alpha
- **WHEN** render declares `alpha: float = fixed_tick.alpha`
- **THEN** render receives the fixed-step fractional remainder ratio after all fixed repetitions

#### Scenario: Fixed handler cannot read completion alpha
- **WHEN** an `on fixed_tick` handler reads `fixed_tick.alpha`
- **THEN** semantic analysis reports that completion field `alpha` is available only to downstream phases

### Requirement: Activation commit boundary
Every phase activation, including every repetition of a periodic phase, SHALL execute its handler graph and in-activation event cascade to completion and then commit buffered structural commands before any subsequent activation begins. Trait value writes SHALL become visible according to handler graph order within the activation.

#### Scenario: Repetition commits before next repetition
- **WHEN** fixed_tick repetition one spawns an entity and repetition two follows
- **THEN** the spawn is committed before repetition two selects entities

#### Scenario: Tick observes completed fixed simulation
- **WHEN** fixed_tick repeats and issues trait and structural changes
- **THEN** tick begins only after every repetition and commit has completed

### Requirement: A periodic phase's dt is a constant expression
Because a periodic phase's synthesized `dt` and `delta` always equal its compile-time `every:` interval, `<phase>.dt` and `<phase>.delta` for a phase with `every:` SHALL be constant expressions. They SHALL be usable wherever a constant is accepted, including `where:`, `reduce:`, `order by:`, and `const` declarations, with the value of the interval. Outside handlers, the name SHALL resolve to the phase declaration. Reading `dt` or `delta` of a phase without `every:` outside a handler for that phase SHALL be a compile error.

#### Scenario: Fixed step in a reducer
- **WHEN** a rule reduces `first = first_hit(physics.sweep(bullet, bullet[Bullet].velocity * fixed_tick.dt, target))` and `fixed_tick` declares `every: 1.0 / 60.0`
- **THEN** compilation accepts it and the sweep uses `delta = velocity * (1.0 / 60.0)`

#### Scenario: Fixed step in a constant
- **WHEN** a `const` block declares `STEP_DISTANCE = BULLET_SPEED * fixed_tick.dt`
- **THEN** compilation accepts it as a compile-time constant

#### Scenario: Fixed step as a duration constant
- **WHEN** a `const` block declares `GRACE = fixed_tick.delta * 6` and `fixed_tick` declares `every: 1s / 60`
- **THEN** compilation accepts it and `GRACE` equals `100ms`

#### Scenario: Variable-rate dt is not a constant
- **WHEN** a `reduce:` expression reads `tick.dt` or `tick.delta` and `tick` declares no `every:`
- **THEN** compilation reports a source-located error saying that the field is only available inside `tick` handlers

### Requirement: The root frame's step is finite and not negative
Every root `frame` occurrence SHALL carry a `dt` that is finite and at least `0.0`, and a `delta` that is at least `0s`. Where the root frame enters the runtime, a negative, infinite or NaN `dt` SHALL become `0.0`, and a negative `delta` SHALL become `0s`. This SHALL hold for the generated frame loop, for a host that builds its own `main()`, and for the headless driver. The generated loop SHALL clean `dt` before it computes `delta` from it. So a phase field initialized from `frame.dt` or `frame.delta` is never negative, and a periodic phase's accumulator never moves backward.

#### Scenario: A negative host value
- **WHEN** the host drives a frame with `dt = -0.5`
- **THEN** `tick.dt` is `0.0` and `tick.delta` is `0s` in that frame, and no periodic phase activates because of it

#### Scenario: A non-finite host value
- **WHEN** the generated loop drives a frame with a NaN or infinite `dt`
- **THEN** `tick.dt` is `0.0` and `tick.delta` is `0s` in that frame

#### Scenario: A host injects a negative delta
- **WHEN** a host's own `main()` injects a frame with `dt = 0.016` and a negative `delta`
- **THEN** `tick.dt` is `0.016` and `tick.delta` is `0s` in that frame

#### Scenario: An ordinary value is unchanged
- **WHEN** the host drives a frame with `dt = 0.016`
- **THEN** `tick.dt` is `0.016` and `tick.delta` is `duration(0.016)`

### Requirement: Periodic phase rate
A phase that declares `every:` SHALL accept one optional clause `rate: <expression>`. The expression SHALL be a named field read, `Entity[Trait].field` with an optional module prefix, whose field has type `float` and is not a cell field of a dimensioned trait. A `clamped` field SHALL be accepted. Each of these SHALL be a source-located error: `rate:` on a phase without `every:`; a constant; any other expression form; a field of another type; a cell field; a second `rate:` clause.

At each root occurrence the phase SHALL read the field once, when its batch starts and before its accumulator grows, from committed state. Writes committed by the phase's upstream phases in the same root occurrence SHALL be visible to that read; writes made by the phase's own activations or by later phases SHALL apply from the next root occurrence. The effective rate SHALL be 0 when the value is NaN or below 0, 1024 when it is above 1024, and the value itself otherwise. The accumulator SHALL grow by the root's `dt` times the effective rate. Everything else SHALL be as for a periodic phase without `rate:`: the number of due steps, the `max:` cap, the dropping of capped whole steps, `dt` and `delta` equal to `every` for every activation, and `alpha`.

When the named entity does not exist or does not carry the trait, the effective rate SHALL be 0 and the runtime SHALL report a fault that names the entity, the trait and the field. The fault SHALL be reported once when the source goes missing, and again only after the source has been present at a later root occurrence and then goes missing again.

A phase without `rate:` SHALL behave as before. A phase whose rate field holds 1.0 SHALL perform the same activations as the same phase without `rate:`.

#### Scenario: Normal speed
- **WHEN** a phase with `every: 1s / 60` and `rate: Clock[Pace].scale` runs 60 frames of 1/60 s while `scale` is 1.0
- **THEN** it activates 60 times, as it would without `rate:`

#### Scenario: Slow motion
- **WHEN** `scale` is 0.5 for 60 frames of 1/60 s
- **THEN** the phase activates 30 times, each with `delta = 1s / 60`

#### Scenario: Stopped
- **WHEN** `scale` is 0.0
- **THEN** the phase does not activate and its `alpha` keeps its last value

#### Scenario: Fast-forward
- **WHEN** `scale` is 4.0 and `max:` is 8
- **THEN** the phase activates 4 times per frame of 1/60 s

#### Scenario: Bad value
- **WHEN** the rate field is an unclamped `float` that holds NaN or -1.0
- **THEN** the phase does not activate

#### Scenario: Write from the phase itself takes effect next frame
- **WHEN** a handler of the phase writes `scale = 0.0` in the first of two steps due this frame
- **THEN** the second step still runs, and no step runs in the next frame

#### Scenario: Write from an upstream phase takes effect this frame
- **WHEN** an `input` handler writes `scale = 0.0` and the periodic phase runs after `input`
- **THEN** the periodic phase runs no step in that frame

#### Scenario: Missing source
- **WHEN** the rate's named entity is destroyed, and the following frames run
- **THEN** the phase runs no step in those frames, and exactly one fault naming the entity, the trait and the field is reported

#### Scenario: Rate on a non-periodic phase
- **WHEN** a phase without `every:` declares `rate:`
- **THEN** the compiler reports an error saying `rate:` needs a periodic phase

#### Scenario: Rate is not a named field read
- **WHEN** a phase declares `rate: 0.5` or `rate: Clock[Pace].scale * 2.0`
- **THEN** the compiler reports an error saying `rate:` takes one named `float` field
