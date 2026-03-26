## Purpose

This spec defines the standard lifecycle event types declared in `std.core`. These events are the authoritative source of lifecycle event field shapes; no separate hardcoded table exists in the compiler.

## Requirements

### Requirement: Standard lifecycle events declared in std.core
The `std.core` module SHALL declare all built-in lifecycle event types as `pub event` declarations. Events with a `dt` field use a colon body; events with no fields use the marker form (no colon, no body). The canonical declarations are:

```cactus
pub event tick:
    let dt: float

pub event fixed_tick:
    let dt: float

pub event late_tick:
    let dt: float

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
The compiler's module resolver SHALL pre-load `std.core` pub symbols before analyzing any user module, so lifecycle event types (`tick`, `fixed_tick`, `late_tick`, `spawn`, `destroy`, `input`, `load`, `unload`) are always resolvable without an explicit `use std.core` declaration.

#### Scenario: on tick handler resolves without use std.core
- **WHEN** a user module has `on tick:` with no `use std.core` import
- **THEN** the semantic analyzer resolves the `tick` event type successfully

#### Scenario: on PlayerDamaged still requires local declaration
- **WHEN** a user module has `on PlayerDamaged:` without a local `event PlayerDamaged:` declaration
- **THEN** the semantic analyzer reports an error "undeclared event 'PlayerDamaged'"
