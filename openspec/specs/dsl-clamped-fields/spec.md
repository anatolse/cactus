## Purpose

Let a numeric `var` field declare bounds that every value read from it stays inside, with projection rules that keep commuting writes commuting and make the result independent of which rules read the field.

## Requirements

### Requirement: Clamped field declaration
A `var` field of type `int`, `float` or `duration` SHALL accept `clamped(min = a, max = b)` after its type, with at least one of `min` and `max`. Each bound SHALL be a constant expression or a `let` field of the same trait, of the field's type. A missing side SHALL be the type's own limit on that side. A `clamped` modifier on a `let` field, on another type, with no bound, or with a bound that reads anything else SHALL be a source-located error. Constant bounds with `min > max` SHALL be an error.

#### Scenario: Two-sided bounds from a let field
- **WHEN** a trait declares `let max_hp: float = 100.0` and `var hp: float clamped(min = 0.0, max = max_hp) = 100.0`
- **THEN** compilation accepts it

#### Scenario: A clamped duration
- **WHEN** a trait declares `var fatigue: duration clamped(min = 0s) = 0s`
- **THEN** compilation accepts it

#### Scenario: A bound reading a var field
- **WHEN** a field declares `clamped(max = limit)` and `limit` is a `var` field
- **THEN** the compiler reports an error naming `limit` and saying a bound must be fixed for the field's life

#### Scenario: Clamped let field
- **WHEN** a `let` field declares `clamped(min = 0)`
- **THEN** the compiler reports an error suggesting `in range` for `let` fields

### Requirement: A clamped field holds only values inside its bounds
Every value read from a clamped field SHALL be inside its bounds. A value given at creation (a declared entity, `spawn`, `add`, `project`), by an Assign or General write, or by `set` SHALL be projected onto the interval at that write: below `min` becomes `min`, above `max` becomes `max`, and NaN becomes `min`, or `max` when there is no `min`. When `let` bounds evaluate to `min > max` at creation, the creation SHALL do nothing and report a fault naming the trait and field, as a failed ranged check does.

#### Scenario: Assignment above the maximum
- **WHEN** a handler assigns `hp = 150.0` and `max_hp` is 100
- **THEN** `hp` holds 100

#### Scenario: NaN assignment
- **WHEN** a handler assigns NaN to a field with `clamped(min = 0.0, max = 1.0)`
- **THEN** the field holds 0

#### Scenario: Spawn with crossed let bounds
- **WHEN** a spawn gives `lo = 5` and `hi = 1` to a trait whose field declares `clamped(min = lo, max = hi)`
- **THEN** no entity is created and a fault naming the field is reported

### Requirement: Accumulation is projected once per activation
During an activation, a clamped field SHALL have an exact value: the value it held when the activation began, plus the exact sum of every Accum delta written to it so far in that activation, with no rounding, wrapping or projection between deltas. An Assign or General write SHALL replace the exact value with its projected value. A read SHALL give the projection of the exact value, rounded once for `float`, and SHALL NOT change the exact value. When the activation ends, the field SHALL hold the projection of the exact value. So the value the field holds after an activation SHALL NOT depend on the order of unordered Accum writes, nor on whether or where any handler reads the field. Join writes SHALL be projected and SHALL keep commuting. Clamping SHALL NOT change a write's kind or add a schedule edge.

#### Scenario: Unordered deltas at the bound
- **WHEN** a field with `clamped(max = 100)` holds 100 and two unordered handlers add 1 and -1
- **THEN** the field holds 100 after the activation, in either order, and no schedule edge joins the handlers

#### Scenario: A reader between two writers does not change the result
- **WHEN** `hp` is 100 with `max_hp` 100, handler A runs `hp += 50.0`, handler R reads `hp`, then handler B runs `hp -= 50.0`, all in one activation
- **THEN** R reads 100
- **AND** `hp` holds 100 after the activation, as it does in the same program without R

#### Scenario: The next activation starts from the projected value
- **WHEN** one activation leaves an exact value of 150 on a field with `clamped(max = 100)`, and the next activation subtracts 50
- **THEN** the field holds 50 after the second activation

#### Scenario: An int sum does not wrap
- **WHEN** an `int` field with `clamped(min = 0)` holds 2147483647 and a handler adds 1
- **THEN** the field holds 2147483647

#### Scenario: A duration countdown stops at zero
- **WHEN** a field `duration clamped(min = 0s)` holds `10ms` and a handler subtracts `1s`
- **THEN** the field holds `0s`
