## Purpose
Define the bounded foreach statement for finite snapshot iteration in handler code.

## Requirements

### Requirement: Bounded foreach iterates finite list snapshots
The DSL SHALL support a bounded foreach statement inside system event handlers for iterating over finite `list[T]` values.

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
Bounded foreach SHALL NOT introduce open-ended control flow. The language SHALL continue to reject `while` loops, numeric/indexed `for` loops, and `break` / `continue` statements in v1.

#### Scenario: While loop remains unsupported
- **WHEN** source contains `while condition:` in author code
- **THEN** the compiler reports that general loops are not supported

#### Scenario: Numeric for loop remains unsupported
- **WHEN** source contains a numeric/indexed loop form such as `for i = 0; i < 10; i += 1:`
- **THEN** the compiler reports that numeric loops are not supported