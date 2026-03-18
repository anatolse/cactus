## ADDED Requirements

### Requirement: Field access validation — mandatory alias.field in system handlers
The semantic analyzer SHALL enforce that all trait field accesses within system handler bodies use the `alias.field` form. Unqualified bare identifiers that resolve to trait fields SHALL be rejected with an error.

If no `as` alias is declared in the `filter:` entry, the trait name itself is the implicit alias. Both `TraitName.field` and `alias.field` (if declared) are valid; bare `field` alone is not.

#### Scenario: Alias.field access accepted
- **WHEN** a system has `filter: Position as pos` and the handler body contains `pos.x += 1.0`
- **THEN** the analyzer accepts the access and resolves `pos.x` to the `x` field of `Position`

#### Scenario: Trait name as implicit alias accepted
- **WHEN** a system has `filter: Position` (no alias) and the handler body contains `Position.x += 1.0`
- **THEN** the analyzer accepts `Position.x` as the implicit-alias access to `Position`'s `x` field

#### Scenario: Bare field name rejected
- **WHEN** a system filters on `Position` and the handler body contains bare `x += 1.0`
- **THEN** the analyzer reports an error: "unqualified field access 'x' not allowed; use 'Position.x' or declare an alias with 'as'"

#### Scenario: Wrong alias rejected
- **WHEN** a system has `filter: Position as pos` and the handler body contains `Position.x += 1.0`
- **THEN** the analyzer accepts it (both alias and trait name are valid access paths for the same trait)

#### Scenario: Accessing field from non-filtered trait rejected
- **WHEN** a system has `filter: Position` and the handler body contains `Velocity.dx`
- **THEN** the analyzer reports an error: "'Velocity' is not in the filter clause of this system"

---

### Requirement: Local variable scope in system handlers
The semantic analyzer SHALL maintain a per-handler local variable scope. `let` declarations introduce immutable bindings; `var` declarations introduce mutable bindings. Re-declaration of an existing local in the same scope SHALL produce an error. `let` bindings SHALL NOT be reassigned after declaration.

#### Scenario: Let binding immutable after declaration
- **WHEN** a handler declares `let speed = 5.0` and then assigns `speed = 6.0`
- **THEN** the analyzer reports an error: "cannot reassign immutable binding 'speed'"

#### Scenario: Var binding reassignable
- **WHEN** a handler declares `var count = 0` and then assigns `count = count + 1`
- **THEN** the analyzer accepts both statements

#### Scenario: Re-declaration in same scope rejected
- **WHEN** a handler declares `let speed = 5.0` and later declares `let speed = 6.0` in the same block
- **THEN** the analyzer reports an error: "redeclaration of local 'speed' in the same scope"

#### Scenario: Local shadows alias name allowed with warning
- **WHEN** a handler declares `let pos = 0.0` where `pos` is also a filter alias
- **THEN** the analyzer reports a warning: "local 'pos' shadows filter alias 'pos'; use a different name to avoid confusion"

---

### Requirement: Implicit `event` object in user event handlers
The semantic analyzer SHALL make an implicit `event` object available in the scope of user-defined event handlers. The `event` object's type is the event being handled; its fields are accessible as `event.fieldname`. The `event` object is read-only (all fields are accessed via `let`-semantics).

This implicit object is NOT available in lifecycle handlers (`tick`, `fixed_tick`, `late_tick`, `input`, `spawn`, `destroy`, `load`, `unload`).

#### Scenario: event.field access in user event handler
- **WHEN** a system handles `on PlayerDamaged():` and `event PlayerDamaged:` has field `var amount: int`
- **THEN** `event.amount` in the handler body resolves to the `amount` field of the event payload

#### Scenario: event.field access in lifecycle handler rejected
- **WHEN** a system handles `on tick(dt: float):` and the body contains `event.something`
- **THEN** the analyzer reports an error: "'event' is not available in lifecycle handlers"

#### Scenario: event object is read-only
- **WHEN** a handler body contains `event.amount = 99`
- **THEN** the analyzer reports an error: "event fields are read-only; cannot assign to 'event.amount'"

---

### Requirement: Targeted emit validation
The semantic analyzer SHALL verify that the expression in an `emit ... to expression` statement evaluates to type `entity_id`. Any other type SHALL produce a compile error.

#### Scenario: Targeted emit with entity_id field accepted
- **WHEN** `emit Damage(amount = 10) to EnemyAI.target` and `EnemyAI.target` is of type `entity_id`
- **THEN** the analyzer accepts the targeted emit

#### Scenario: Targeted emit with non-entity_id expression rejected
- **WHEN** `emit Damage(amount = 10) to Position.x` and `Position.x` is of type `float`
- **THEN** the analyzer reports an error: "emit target must be of type entity_id, got float"

#### Scenario: Targeted emit with spawn expression accepted
- **WHEN** `emit Configure(val = 5) to spawn Enemy()` where `spawn` returns `entity_id`
- **THEN** the analyzer accepts the targeted emit

---

### Requirement: `destroy entity_id` validation
The semantic analyzer SHALL verify that when `destroy` is given an expression argument, the expression evaluates to type `entity_id`. When called without argument, it removes the current entity (always valid inside a system handler).

#### Scenario: Destroy with entity_id expression accepted
- **WHEN** `destroy PlayerComposition.gun` and `PlayerComposition.gun` is of type `entity_id`
- **THEN** the analyzer accepts the destroy statement

#### Scenario: Destroy with non-entity_id expression rejected
- **WHEN** `destroy Position.x` and `Position.x` is of type `float`
- **THEN** the analyzer reports an error: "destroy argument must be of type entity_id, got float"

---

### Requirement: Event dispatch model — semantic validation of cascade and ordering
The semantic analyzer SHALL validate event handler ordering consistency: if a system handles event A and emits event B, and another system handles event B and emits event A, this forms a potential cycle. The analyzer SHALL warn (not error) on detected event cycles, noting that they may loop up to `max_cascade_depth` times.

#### Scenario: Event cycle warning emitted
- **WHEN** SystemA handles EventX and emits EventY, and SystemB handles EventY and emits EventX
- **THEN** the analyzer reports a warning: "potential event cycle detected: EventX → EventY → EventX"

#### Scenario: Non-cycling event chain accepted without warning
- **WHEN** SystemA emits EventX and SystemB handles EventX without emitting any events
- **THEN** the analyzer accepts this without warnings

---

## MODIFIED Requirements

### Requirement: Filter clause aliases for trait fields
The semantic analyzer SHALL support `as` aliases in system `filter:` block entries. When an `as` alias is declared, both the alias and the trait name are valid access paths for that trait's fields. When no `as` alias is declared, the trait name is the only valid access path.

All filter clause access patterns now use `alias.field` or `TraitName.field` — unqualified field access is no longer permitted.

#### Scenario: Filter alias used for field access
- **WHEN** a system has a filter entry `phys.Body as body`
- **THEN** `body.x` resolves to the `x` field of `Body`

#### Scenario: Filter with no alias uses trait name as access path
- **WHEN** a system has a filter entry `Position` with no alias
- **THEN** `Position.x` resolves to the `x` field of `Position`

#### Scenario: Qualified module path trait accessed via alias
- **WHEN** a system has `use phys.body as pb` and filter entry `phys.Body as b`
- **THEN** `b.velocity` resolves to `Body.velocity` correctly

---

### Requirement: Trait field disambiguation in systems
The semantic analyzer SHALL require `alias.field` or `TraitName.field` access for all trait fields. The "unqualified access if unique" rule is removed. All field access is qualified.

#### Scenario: Qualified access required regardless of uniqueness
- **WHEN** a system filters `[Position, Velocity]` where fields are unique across traits
- **THEN** accessing bare `x` produces an error: "unqualified field access 'x' not allowed; use 'Position.x'"

#### Scenario: Alias.field always accepted
- **WHEN** a system has `filter: [Position as pos, Velocity as vel]`
- **THEN** `pos.x` and `vel.dx` are both accepted as valid field accesses

---

### Requirement: Event validation
The semantic analyzer SHALL verify that all `emit` statements reference declared events. For broadcast `emit` (no `to` clause), all systems that handle the event will receive it. For targeted `emit ... to entity_id`, only the matching entity receives it.

User event handler parameter lists are removed — handlers use the implicit `event` object instead of parameters. The analyzer SHALL verify that `on EventName():` has an empty parameter list for user-defined events and that the event name is declared.

#### Scenario: Emit of declared event accepted
- **WHEN** a system handler contains `emit Damage(amount = 10)` and `event Damage:` is declared with field `amount: int`
- **THEN** the analyzer accepts the emit statement

#### Scenario: Emit of undeclared event rejected
- **WHEN** a system handler contains `emit Foo()` and no `event Foo:` is declared
- **THEN** the analyzer reports an error: "undeclared event 'Foo'"

#### Scenario: User event handler with parameters rejected
- **WHEN** `on PlayerDamaged(amount: int):` appears with a non-empty parameter list
- **THEN** the analyzer reports an error: "user event handlers must have empty parameter list; access fields via 'event.amount'"

#### Scenario: Lifecycle handler parameters validated as before
- **WHEN** `on tick(dt: float):` appears
- **THEN** the analyzer accepts it (lifecycle handlers retain their parameter signatures)

---

### Requirement: Lifecycle handler signature validation
The semantic analyzer SHALL verify that lifecycle handlers have the correct parameter signatures. Updated to include new lifecycle names.

| Handler | Expected signature |
|---------|-------------------|
| `on tick(dt: float):` | one `float` param named `dt` |
| `on fixed_tick(dt: float):` | one `float` param named `dt` |
| `on late_tick(dt: float):` | one `float` param named `dt` |
| `on input():` | empty param list |
| `on spawn():` | empty param list |
| `on destroy():` | empty param list |
| `on load():` | empty param list |
| `on unload():` | empty param list |

#### Scenario: on fixed_tick with wrong param type rejected
- **WHEN** `on fixed_tick(dt: int):` appears
- **THEN** the analyzer reports an error: "lifecycle handler 'fixed_tick' expects parameter 'dt: float'"

#### Scenario: on input with parameters rejected
- **WHEN** `on input(key: int):` appears
- **THEN** the analyzer reports an error: "lifecycle handler 'input' does not accept parameters"

#### Scenario: on tick with correct signature accepted
- **WHEN** `on tick(dt: float):` appears
- **THEN** the analyzer accepts it (existing behavior preserved)
