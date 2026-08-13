## Purpose
Define the bounded foreach statement for finite snapshot iteration in handler code.

## Requirements

### Requirement: Bounded foreach iterates finite list snapshots
The DSL SHALL support a bounded foreach statement inside rule event handlers for iterating over finite `list[T]` values.

The iterable expression SHALL be evaluated exactly once before iteration begins. The resulting list value SHALL be treated as an immutable snapshot for the duration of the loop. The loop variable SHALL be a read-only binding scoped to the loop body.

```cactus
for hit in phys.query_overlap_all(self, HAZARD_MASK, self):
    emit Damage to hit.entity:
        amount = 1
```

#### Scenario: Foreach consumes physics query contacts
- **WHEN** a handler executes `for hit in phys.query_overlap_all(self, mask, self):`
- **THEN** the loop body executes once for each contact in the returned list snapshot
- **AND** `hit` is bound to the element value for the current iteration

#### Scenario: Foreach over empty list executes zero times
- **WHEN** the iterable expression evaluates to an empty list
- **THEN** the loop body is skipped

#### Scenario: Iterable expression evaluated once
- **WHEN** a foreach iterable expression calls a query function
- **THEN** the query is evaluated once before loop execution rather than once per iteration

### Requirement: Bounded foreach is not a general loop facility
Bounded foreach SHALL NOT introduce open-ended control flow. The language SHALL continue to reject `while` loops, C-style numeric/indexed `for` loops (an explicit init/condition/increment header), and `break` / `continue` statements in v1. The sole sanctioned form of counted iteration is the `range(begin, end, step=1)` intrinsic iterable defined below; it does not reopen this boundary because its bounds are evaluated once before the loop starts and no combination of runtime values can make it open-ended.

#### Scenario: While loop remains unsupported
- **WHEN** source contains `while condition:` in author code
- **THEN** the compiler reports that general loops are not supported

#### Scenario: Numeric for loop remains unsupported
- **WHEN** source contains a C-style numeric/indexed loop form such as `for i = 0; i < 10; i += 1:`
- **THEN** the compiler reports that numeric loops are not supported

#### Scenario: break and continue remain unsupported
- **WHEN** source contains a `break` or `continue` statement inside any loop body
- **THEN** the compiler reports that `break`/`continue` are not supported

### Requirement: range() intrinsic provides bounded counted iteration
The DSL SHALL support a `range(begin: int, end: int, step: int = 1)` intrinsic accepted only as the direct iterable expression of a `for <var> in range(...):` statement. `range(...)` SHALL NOT be usable as a general expression: it SHALL NOT be assignable to a `let`/`var`, passed as a function argument, or otherwise referenced outside that syntactic position. The loop variable SHALL be a read-only `int` binding scoped to the loop body.

`begin`, `end`, and `step` SHALL each be evaluated exactly once before iteration begins, matching the "iterable expression evaluated once" guarantee that applies to bounded foreach generally. When `step` is omitted it SHALL default to `1`.

```cactus
for k in range(0, PARTICLE_COUNT):
    let angle = k * (TAU / PARTICLE_COUNT)
    spawn ParticleTemplate:
        Particle:
            velocity = vec2(cos(angle), sin(angle)) * PARTICLE_SPEED
```

#### Scenario: Ascending iteration
- **WHEN** a handler executes `for k in range(0, 5):`
- **THEN** the loop body executes 5 times with `k` bound to `0, 1, 2, 3, 4` in order

#### Scenario: Descending iteration
- **WHEN** a handler executes `for k in range(5, 0, -1):`
- **THEN** the loop body executes 5 times with `k` bound to `5, 4, 3, 2, 1` in order

#### Scenario: Default step is 1
- **WHEN** a handler executes `for k in range(0, 3):` with no third argument
- **THEN** the loop behaves identically to `for k in range(0, 3, 1):`

#### Scenario: begin, end, and step are evaluated once
- **WHEN** a `range(...)` argument is an expression with an observable side effect, such as a call to a function that mutates a trait field
- **THEN** that argument expression is evaluated exactly once, before the first loop iteration, regardless of how many times the loop body subsequently runs

#### Scenario: range() rejected outside a for-loop iterable position
- **WHEN** source contains `let r = range(0, 10)`, or `range(0, 10)` used as a function call argument
- **THEN** the compiler reports that `range()` is only valid as the iterable of a `for` statement

### Requirement: range() is total regardless of runtime step direction
A `range(...)` loop SHALL NOT be able to execute indefinitely or fail regardless of its runtime `begin`, `end`, and `step` values, since `step` need not be a compile-time constant and an author-supplied value could otherwise describe an empty or contradictory span.

#### Scenario: Zero step produces zero iterations
- **WHEN** a handler executes `for k in range(0, 10, 0):`
- **THEN** the loop body executes zero times

#### Scenario: Step direction mismatched with bounds produces zero iterations
- **WHEN** a handler executes `for k in range(0, 10, -1):` (an ascending span with a negative step), or `for k in range(10, 0, 1):` (a descending span with a positive step)
- **THEN** the loop body executes zero times

### Requirement: range() iteration performs no heap allocation
Unlike other bounded-foreach iterable forms, which snapshot a `list[T]` value, the generated code for a `for <var> in range(...):` loop SHALL NOT construct a `list[T]` or other heap-backed container. It SHALL lower to a native counting loop driven directly by the evaluated `begin`/`end`/`step` values.

#### Scenario: No list snapshot is constructed for range()
- **WHEN** the `cpp-entt` backend compiles `for k in range(0, PARTICLE_COUNT):`
- **THEN** the generated code does not construct a `std::vector` or other heap-backed container to represent the range