## MODIFIED Requirements

### Requirement: Event validation
The semantic analyzer SHALL verify that all `emit` statements reference declared events. All event handlers SHALL have no parameter list (the new syntax has none). The analyzer SHALL NOT enforce separate rules for lifecycle vs. user event handlers — both are validated by resolving the event name against declared event types (including std.core lifecycle events).

#### Scenario: Emit of declared event accepted
- **WHEN** a system handler contains `emit Damage(amount = 10)` and `event Damage:` is declared with field `amount: int`
- **THEN** the analyzer accepts the emit statement

#### Scenario: Emit of undeclared event rejected
- **WHEN** a system handler contains `emit Foo()` and no `event Foo:` is declared
- **THEN** the analyzer reports an error "undeclared event 'Foo'"

#### Scenario: Handler for undeclared event rejected
- **WHEN** `on GhostSignal:` appears and no `event GhostSignal` is declared in scope
- **THEN** the analyzer reports an error "undeclared event 'GhostSignal'"

#### Scenario: Handler for stdlib lifecycle event accepted
- **WHEN** `on tick:` appears in a system body
- **THEN** the analyzer resolves `tick` from std.core and accepts the handler

### Requirement: Implicit event variable binding in event handlers
The semantic analyzer SHALL introduce one implicit read-only local variable in every event handler body:
- If the handler has an `as alias` clause, the variable name is the alias; otherwise it is the event name.
- The variable's type is the resolved event struct type.
- The variable is read-only: assigning to any field via this variable SHALL be rejected.
- The variable is scoped to the handler body only.
- A handler alias that conflicts with a name already bound in the enclosing system scope (e.g., a filter alias) SHALL produce an error.

This replaces the previous `event` implicit object (for user events) and the previous injected `dt` parameter (for lifecycle events).

#### Scenario: tick.dt access accepted
- **WHEN** `on tick:` handler body contains `pos.x = pos.x + vel.x * tick.dt`
- **THEN** the analyzer resolves `tick.dt` as `float` and accepts the expression

#### Scenario: tick alias access accepted
- **WHEN** `on tick as t:` handler body contains `pos.x = pos.x + t.dt`
- **THEN** the analyzer resolves `t.dt` as the `dt` field of the `tick` event type

#### Scenario: User event name access accepted
- **WHEN** `on PlayerDamaged:` handler body contains `h.health = h.health - PlayerDamaged.amount`
- **THEN** the analyzer resolves `PlayerDamaged.amount` as `int`

#### Scenario: User event alias access accepted
- **WHEN** `on PlayerDamaged as dmg:` handler body contains `h.health = h.health - dmg.amount`
- **THEN** the analyzer resolves `dmg.amount` as `int`

#### Scenario: Event variable field assignment rejected
- **WHEN** a handler body contains `tick.dt = 0.0`
- **THEN** the analyzer reports an error: "event fields are read-only; cannot assign to 'tick.dt'"

#### Scenario: Handler alias conflicts with filter alias rejected
- **WHEN** a system has filter `Position as t` and a handler `on tick as t:`
- **THEN** the analyzer reports an error: "handler alias 't' conflicts with filter alias 't' already in scope"

#### Scenario: Spawn handler body has no accessible event fields
- **WHEN** `on spawn:` handler body contains `spawn.dt`
- **THEN** the analyzer reports an error: "event 'spawn' has no field 'dt'"

## REMOVED Requirements

### Requirement: Lifecycle handler signature validation
**Reason**: Lifecycle events are now declared in `std.core` as regular `event` blocks. Their field shapes are validated uniformly via event type resolution (see "Implicit event variable binding in event handlers"). There is no longer a hardcoded parameter signature table in the analyzer.
**Migration**: Replace `on tick(dt: float):` with `on tick:` and access `dt` as `tick.dt`. Replace `on PlayerDamaged():` with `on PlayerDamaged:` and access fields as `PlayerDamaged.field` instead of `event.field`.
