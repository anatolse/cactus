## ADDED Requirements

### Requirement: Four-phase per-frame update model
The DSL SHALL define four named lifecycle event handlers that map to a fixed frame execution order. A system participates in a phase by declaring the corresponding handler. A system MAY declare handlers for multiple phases.

| Handler name | Phase | `dt` parameter | Runs per frame |
|---|---|---|---|
| `on input()` | Input | none | Once |
| `on fixed_tick(dt: float)` | Physics | fixed timestep | 0..N (accumulator model) |
| `on tick(dt: float)` | Update | variable | Once |
| `on late_tick(dt: float)` | Post-update | variable | Once |

The execution order within a frame SHALL be:

```
on input()          [once per frame]
  → event cascade   [depth ≤ max_cascade_depth]
on fixed_tick(dt)   [0..N times, accumulator-based]
  → event cascade   [per fixed step, depth ≤ max_cascade_depth]
on tick(dt)         [once per frame]
  → event cascade   [depth ≤ max_cascade_depth]
on late_tick(dt)    [once per frame]
  → event cascade   [depth > max_cascade_depth → deferred to next frame]
RENDER              [backend, not user code]
```

#### Scenario: System with input handler runs once per frame before physics
- **WHEN** a system declares `on input():` and `on fixed_tick(dt: float):`
- **THEN** the runtime executes `on input()` for all matching entities before executing any `on fixed_tick()` in the same frame

#### Scenario: System with only late_tick participates only in post-update phase
- **WHEN** a system declares only `on late_tick(dt: float):`
- **THEN** the runtime does not invoke the system during input, fixed_tick, or tick phases

#### Scenario: Multiple handlers in same system all execute
- **WHEN** a system declares `on tick(dt: float):` and `on late_tick(dt: float):`
- **THEN** both handlers execute each frame, tick before late_tick, using the same filter

### Requirement: `on input()` handler — no dt parameter
The `on input()` handler SHALL accept exactly zero parameters. Declaring `on input(dt: float):` SHALL be a parse error.

#### Scenario: on input() with no parameters accepted
- **WHEN** `on input():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "input"` and an empty parameter list

#### Scenario: on input() with a parameter rejected
- **WHEN** `on input(dt: float):` appears in a system body
- **THEN** the parser reports an error: `input` handler takes no parameters

### Requirement: `on fixed_tick(dt: float)` handler — physics phase
The `on fixed_tick(dt: float)` handler SHALL accept exactly one parameter named `dt` of type `float`. The value of `dt` SHALL be a constant fixed timestep determined by the backend/runtime (not the variable frame delta). This handler SHALL run zero or more times per frame based on an accumulator model.

The keyword `fixed_tick` SHALL be added to the keyword list and to the `event_name` production.

#### Scenario: on fixed_tick handler parsed with dt parameter
- **WHEN** `on fixed_tick(dt: float):` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "fixed_tick"` and one parameter `dt: float`

#### Scenario: on fixed_tick with wrong parameter type rejected
- **WHEN** `on fixed_tick(dt: int):` appears in a system body
- **THEN** the semantic analyzer reports a type error: the `fixed_tick` handler's `dt` parameter must be `float`

#### Scenario: on fixed_tick with no parameters rejected
- **WHEN** `on fixed_tick():` appears in a system body
- **THEN** the parser or semantic analyzer reports an error: `fixed_tick` handler requires a `dt: float` parameter

### Requirement: `on late_tick(dt: float)` handler — post-update phase
The `on late_tick(dt: float)` handler SHALL accept exactly one parameter named `dt` of type `float`, with the variable frame delta time. This handler runs once per frame after all `on tick()` handlers. Events fired from `on late_tick()` that exceed the cascade depth limit SHALL be deferred to the next frame.

The keyword `late_tick` SHALL be added to the keyword list and to the `event_name` production.

#### Scenario: on late_tick handler parsed with dt parameter
- **WHEN** `on late_tick(dt: float):` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "late_tick"` and one parameter `dt: float`

#### Scenario: Events from late_tick at cascade limit are deferred
- **WHEN** a `late_tick` handler fires an event at cascade depth equal to `max_cascade_depth`
- **THEN** the runtime defers the resulting event cascade to the next frame's input phase

### Requirement: Handler filter applies to all phases within a system
The `filter:` and `exclude:` clauses of a `system` declaration SHALL apply uniformly to all handlers within that system. A system cannot have different filters for different phases.

#### Scenario: Single filter applies across tick and late_tick
- **WHEN** a system declares `filter: Transform, FollowCamera` and both `on tick():` and `on late_tick():`
- **THEN** both handlers operate on entities that have both `Transform` and `FollowCamera` traits

### Requirement: `fixed_tick` and `late_tick` added to event_name production
The EBNF `event_name` production SHALL be extended to include `fixed_tick` and `late_tick` alongside the existing `tick`, `spawn`, `destroy`, `load`, `unload`, and `input`.

```ebnf
event_name = "tick" | "fixed_tick" | "late_tick"
           | "spawn" | "destroy" | "load" | "unload"
           | "input" | IDENTIFIER ;
```

#### Scenario: fixed_tick recognized as lifecycle name
- **WHEN** `on fixed_tick(dt: float):` appears in a system
- **THEN** the parser recognizes `fixed_tick` as a reserved lifecycle event name and not a user-defined event

#### Scenario: late_tick recognized as lifecycle name
- **WHEN** `on late_tick(dt: float):` appears in a system
- **THEN** the parser recognizes `late_tick` as a reserved lifecycle event name and not a user-defined event
