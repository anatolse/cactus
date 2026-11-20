## Purpose

Define the game clock of the standard library: one `Pace` value that scales how fast `fixed_tick` runs, while `tick`, `late_tick` and `render` keep real frame time.

## Requirements

### Requirement: std.core declares the game pace
`std.core` SHALL declare `pub single trait Pace` with one field `persist var scale: float clamped(min = 0.0, max = 1024.0) = 1.0`, and a named entity `pub entity Clock` that carries `Pace` and `KeepOnLoad`. `std.core.fixed_tick` SHALL declare `rate: Clock[Pace].scale`. `tick`, `late_tick` and `render` SHALL NOT be scaled by `Pace`.

#### Scenario: Default pace
- **WHEN** a program never writes `Pace.scale`
- **THEN** `fixed_tick` runs as it did before this capability existed

#### Scenario: Hit-stop
- **WHEN** a `fixed_tick` rule writes `core.Clock[core.Pace].scale = 0.0`
- **THEN** from the next frame `fixed_tick` handlers do not run, and `tick` and `render` handlers still run every frame

#### Scenario: Resume
- **WHEN** a `tick` rule writes `scale = 1.0` after some frames at 0.0
- **THEN** `fixed_tick` resumes with no catch-up for the stopped time

#### Scenario: Out-of-range write
- **WHEN** a rule writes `scale = -1.0` or `scale = 5000.0`
- **THEN** a later read of `scale` gives 0.0 or 1024.0

#### Scenario: Clock survives scene cleanup
- **WHEN** an `unload` event runs `SceneCleanup`
- **THEN** `Clock` still exists and `fixed_tick` keeps its pace

#### Scenario: Saved pace
- **WHEN** a world with `scale = 0.5` is saved and restored
- **THEN** `scale` is 0.5 after the restore
