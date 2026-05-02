## Purpose

This spec defines the standard lifecycle event types declared in `std.core`. These events are the authoritative source of lifecycle event field shapes; no separate hardcoded table exists in the compiler.

## Requirements

### Requirement: Standard lifecycle events declared in std.core
The `std.core` module SHALL declare all built-in lifecycle event types as `pub event` declarations. Events with a `dt` field use a colon body with bare event-field syntax; events with no fields use the marker form (no colon, no body). The canonical declarations are:

```cactus
pub event tick:
    dt: float

pub event fixed_tick:
    dt: float

pub event late_tick:
    dt: float

pub event spawn

pub event destroy

pub event input

pub event load

pub event unload
```

These declarations are the authoritative source of lifecycle event field shapes; no separate hardcoded table exists in the compiler.

#### Scenario: tick event has dt field
- **WHEN** `std.core` is analyzed
- **THEN** the `tick` event type is registered with one field `dt: float`

#### Scenario: spawn event has no fields
- **WHEN** `std.core` is analyzed
- **THEN** the `spawn` event type is registered with zero fields

#### Scenario: fixed_tick event has dt field
- **WHEN** `std.core` is analyzed
- **THEN** the `fixed_tick` event type is registered with one field `dt: float`

#### Scenario: late_tick event has dt field
- **WHEN** `std.core` is analyzed
- **THEN** the `late_tick` event type is registered with one field `dt: float`

### Requirement: Lifecycle event types always in scope
The compiler's module resolver SHALL pre-load `std.core` pub symbols before analyzing any user module, so lifecycle event types (`tick`, `fixed_tick`, `late_tick`, `spawn`, `destroy`, `input`, `load`, `unload`) are always resolvable without an explicit `use std.core` declaration. If a user module explicitly declares `use std.core`, that explicit import SHALL be idempotent with the preloaded `std.core` symbols and SHALL NOT produce duplicate-symbol diagnostics for symbols from the same `std.core` module instance.

#### Scenario: on tick handler resolves without use std.core
- **WHEN** a user module has `on tick:` with no `use std.core` import
- **THEN** the semantic analyzer resolves the `tick` event type successfully

#### Scenario: explicit std.core import is idempotent
- **WHEN** a user module explicitly declares `use std.core` and also uses lifecycle handlers such as `on tick:`
- **THEN** semantic analysis and program linking succeed without duplicate-symbol diagnostics for `std.core.Persistent`, `std.core.Parent`, or lifecycle events

#### Scenario: same std.core module is not imported twice through preloading and explicit use
- **WHEN** `std.core` has already been preloaded for a user module and the same module also explicitly imports `std.core`
- **THEN** the linker treats both references as the same module source for duplicate-symbol purposes

#### Scenario: duplicate symbols from distinct modules still fail
- **WHEN** two distinct non-stdlib modules export the same pub symbol into the same importing scope
- **THEN** the linker still reports a duplicate-symbol diagnostic

#### Scenario: on PlayerDamaged still requires local declaration
- **WHEN** a user module has `on PlayerDamaged:` without a local event declaration or imported public event
- **THEN** the semantic analyzer reports an error for the undeclared event
