## Purpose

Define the time primitive of `std.time`: a span with a length and an elapsed time, its progress from 0 to 1, what happens when it ends, and which clock advances it.

## Requirements

### Requirement: Span and progress
`std.time` SHALL declare `pub trait Span` with `let length: duration` and `var elapsed: duration = 0s`, and `pub func progress(span: Span) float`. `progress` SHALL return `elapsed / length` clamped to 0.0..1.0, and 1.0 when `length <= 0s`. All of `std.time` SHALL be written in Cactus with no extern rule or function.

#### Scenario: Progress grows with time
- **WHEN** a span of `length = 100ms` has run 3 steps of `fixed_tick` at `every: 1s / 60`
- **THEN** `progress` is 0.5

#### Scenario: Zero length
- **WHEN** a span has `length = 0s`
- **THEN** `progress` is 1.0

### Requirement: A plain span stops at its end
A span whose entity carries no `Ending` SHALL advance until `elapsed` equals `length`, never beyond it, and SHALL emit one `SpanEnded` event to its own entity on the step that reaches the end. It SHALL then stay at the end and emit nothing more until restarted. A plain span with `length <= 0s` SHALL emit `SpanEnded` on its first step.

#### Scenario: Ends once
- **WHEN** a plain span of `length = 100ms` runs 20 steps of 1/60 s
- **THEN** its entity receives exactly one `SpanEnded`, on step 6, and `progress` is 1.0 from then on

### Requirement: Endings
`std.time` SHALL declare `pub state Ending` with the variants `Expires`, `Repeats` and `Bounces`, where `Bounces` has `var back: bool = false`. An entity SHALL carry at most one ending, as for any state, and `add Ending.V` SHALL swap it.

#### Scenario: Two endings
- **WHEN** an entity declares `Span`, `Ending.Expires` and `Ending.Repeats`
- **THEN** the program fails to compile with an error that names the entity

#### Scenario: Swapping the ending
- **WHEN** a handler runs `add time.Ending.Repeats` on an entity in `Ending.Expires`
- **THEN** from the next step its span repeats and the entity is not destroyed at the end

### Requirement: An expiring span destroys its entity
A span whose entity is in `Ending.Expires` SHALL destroy its entity on the step that reaches the end and emit nothing. With `length <= 0s` it SHALL destroy the entity on its first step.

#### Scenario: Expiry
- **WHEN** an entity carries `Span: length = 100ms` and `Ending.Expires`
- **THEN** it is destroyed on step 6 of `fixed_tick`

### Requirement: A repeating span wraps
A span in `Ending.Repeats` SHALL subtract `length` from `elapsed` on the step that reaches the end, keeping the overshoot below `length`, and emit one `SpanEnded` to its own entity on that step. With `length <= 0s` it SHALL keep `elapsed` at `0s` and emit one `SpanEnded` per step.

#### Scenario: Repeat
- **WHEN** an entity carries `Span: length = 100ms` and `Ending.Repeats` for 12 steps of 1/60 s
- **THEN** it receives two `SpanEnded` events and `elapsed` is `0s` after step 12

### Requirement: A bouncing span runs back
A span in `Ending.Bounces` SHALL run from 0 to `length`, then back to 0, and so on, staying within `0s..length`, and emit nothing.

#### Scenario: Bounce
- **WHEN** an entity carries `Span: length = 100ms` and `Ending.Bounces`
- **THEN** `progress` is 1.0 after step 6 and 0.0 after step 12

### Requirement: Span rules need no ordering
The `std.time` rules for no ending, each variant of `Ending` and each clock SHALL write `elapsed` with no ordering between them, so a program that uses any subset of the endings and clocks SHALL compile without a conflict between them.

#### Scenario: A program that uses one ending
- **WHEN** a program imports `std.time` and only one template carries a span, with `Ending.Expires`
- **THEN** the program compiles with no conflict between the `std.time` rules

#### Scenario: A program with plain spans only
- **WHEN** a program imports `std.time` and its spans carry no `Ending`
- **THEN** the program compiles with no conflict between the `std.time` rules

### Requirement: Span groups
The game-time span rules SHALL form `pub group game_spans` in phase `fixed_tick`, and the real-time span rules `pub group real_spans` in phase `tick`. The rules that emit `SpanEnded` SHALL be ordered within each group, so the handlers `SpanEnded` reaches run in one order.

#### Scenario: Handlers of SpanEnded from two endings
- **WHEN** a program has plain and repeating spans on both clocks and a `SpanEnded` handler assigns a field of a named entity
- **THEN** the program compiles with no conflict

#### Scenario: Ordering after the spans
- **WHEN** a `fixed_tick` rule lists `time.game_spans` in `after:`
- **THEN** it runs after every game-time span rule of that step

### Requirement: Game time and real time
A span SHALL advance once per `fixed_tick` activation by that step's `delta`. A span that carries the marker `RealTime` SHALL instead advance once per `tick` activation by that frame's `delta`, so its end lands on a frame. A span that carries the marker `Paused` SHALL NOT advance and SHALL emit nothing.

#### Scenario: Game clock stopped
- **WHEN** `core.Clock[core.Pace].scale` is 0.0 for one second
- **THEN** a span without `RealTime` does not advance and a span with `RealTime` advances by one second

#### Scenario: Slow motion
- **WHEN** `core.Clock[core.Pace].scale` is 0.5 for one second
- **THEN** a span without `RealTime` advances by 0.5 seconds

#### Scenario: Paused span
- **WHEN** an entity carries `Span`, `Ending.Expires` and `Paused`
- **THEN** its `elapsed` does not change, it emits nothing, and it is not destroyed

### Requirement: Restart
`std.time` SHALL declare `pub event RestartSpan`. Delivered to an entity with a `Span`, it SHALL set `elapsed` to `0s`, set `Ending.Bounces.back` to false when the entity is in that variant, and allow a plain span to end and emit `SpanEnded` again. Delivered to an entity without a `Span`, it SHALL do nothing.

#### Scenario: Restart after the end
- **WHEN** a plain span has ended and its entity receives `RestartSpan`
- **THEN** it runs again from `progress` 0.0 and emits `SpanEnded` when it ends again

#### Scenario: Restart a bouncing span
- **WHEN** a bouncing span is running back and its entity receives `RestartSpan`
- **THEN** it runs forward from `progress` 0.0
