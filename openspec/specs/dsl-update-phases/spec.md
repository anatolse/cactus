## Requirements

### Requirement: Four-phase per-frame update model
The DSL SHALL define four named lifecycle event handlers that map to a fixed frame execution order. A system participates in a phase by declaring the corresponding handler. A system MAY declare handlers for multiple phases.

| Handler name | Phase | Lifecycle data access | Runs per frame |
|---|---|---|---|
| `on input:` | Input | `input` or handler alias | Once |
| `on fixed_tick:` | Physics | `fixed_tick.dt` or handler alias | 0..N (accumulator model) |
| `on tick:` | Update | `tick.dt` or handler alias | Once |
| `on late_tick:` | Post-update | `late_tick.dt` or handler alias | Once |

The execution order within a frame SHALL be:

```
on input:
  → event cascade   [depth ≤ max_cascade_depth]
on fixed_tick:
  → event cascade   [per fixed step, depth ≤ max_cascade_depth]
on tick:
  → event cascade   [depth ≤ max_cascade_depth]
on late_tick:
  → event cascade   [depth > max_cascade_depth → deferred to next frame]
RENDER              [backend, not user code]
```

#### Scenario: System with input handler runs once per frame before physics
- **WHEN** a system declares `on input:` and `on fixed_tick:`
- **THEN** the runtime executes `on input:` for all matching entities before executing any `on fixed_tick:` in the same frame

#### Scenario: System with only late_tick participates only in post-update phase
- **WHEN** a system declares only `on late_tick:`
- **THEN** the runtime does not invoke the system during input, fixed_tick, or tick phases

#### Scenario: Multiple handlers in same system all execute
- **WHEN** a system declares `on tick:` and `on late_tick:`
- **THEN** both handlers execute each frame, tick before late_tick, using the same filter

### Requirement: `on input` handler is parameter-free
The `on input:` handler SHALL use the parameter-free lifecycle-handler form. Input-phase data is accessed through the lifecycle event binding (`input`) or through an explicit alias declared with `on input as <alias>:`.

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

### Requirement: `fixed_tick` and `late_tick` remain lifecycle event names
The lifecycle event name set SHALL include `tick`, `fixed_tick`, `late_tick`, `spawn`, `destroy`, `load`, `unload`, and `input`.

#### Scenario: fixed_tick recognized as lifecycle name
- **WHEN** `on fixed_tick:` appears in a system
- **THEN** the parser and semantic model recognize `fixed_tick` as a reserved lifecycle event name and not a user-defined event

#### Scenario: late_tick recognized as lifecycle name
- **WHEN** `on late_tick:` appears in a system
- **THEN** the parser and semantic model recognize `late_tick` as a reserved lifecycle event name and not a user-defined event
