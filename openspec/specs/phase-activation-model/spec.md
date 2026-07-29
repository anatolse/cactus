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
A phase with `every: interval` SHALL maintain an accumulator advanced by the root runtime frame's `dt`, activate once for each complete interval, and expose synthesized `dt` equal to `interval`. `interval` MUST be a positive compile-time float. `max` MUST be a positive compile-time integer and SHALL cap repetitions per root activation.

If more complete intervals are due than `max`, the scheduler SHALL execute `max` repetitions, drop the remaining unexecuted whole intervals, and preserve only the fractional remainder. This policy SHALL keep synthesized `alpha` in `[0.0, 1.0)`.

#### Scenario: Multiple fixed repetitions run
- **WHEN** the accumulator contains at least three intervals and `max` is at least three
- **THEN** the phase activates three times and each activation observes `phase.dt == interval`

#### Scenario: Catch-up cap drops excess whole steps
- **WHEN** ten intervals are due and `max: 8`
- **THEN** eight repetitions run, two unexecuted whole intervals are dropped, and the fractional remainder is preserved

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
