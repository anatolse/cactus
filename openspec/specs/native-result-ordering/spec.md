# native-result-ordering Specification

## Purpose

Makes every result that native stdlib code hands back to a Cactus program arrive in a declared, deterministic order, so storage order never leaks into program meaning.

## Requirements

### Requirement: Ordered native results declare their order
Every stdlib extern function whose result selects an entity among several, or returns a list, SHALL have one declared order. Every native emitter that can produce more than one occurrence in one generation, and every native consumer that processes occurrences in arrival order, SHALL also have one declared order. A declared order SHALL be one of: stable creation order; hierarchy order (as `std.query` hierarchy snapshots define it); or a named key order whose final tie-break is stable creation order or another total key. The declaration SHALL be part of the function's or emitter's documented contract.

#### Scenario: A list-returning extern documents creation order
- **WHEN** `std.query.children` is documented
- **THEN** its contract states that children are returned in stable creation order

#### Scenario: A keyed order ends in a total tie-break
- **WHEN** a native query orders its results by distance
- **THEN** its declared order breaks exact distance ties by stable creation order

#### Scenario: Pointer events keep their declared sequence
- **WHEN** one frame produces a pointer leave, enter, press, release and click
- **THEN** the occurrences are produced in the declared order leave, enter, press, release, click

### Requirement: Native results do not expose storage order
The result a program observes from a native function, emitter or consumer SHALL follow its declared order only. It SHALL NOT change when the backend's storage order changes without a change in the program's observable world: after a destroy, after a trait is removed and added back, or after a world save and restore.

#### Scenario: Order survives save and restore
- **WHEN** a program saves its world, restores it, and calls the same ordered native function with the same arguments
- **THEN** the result has the same order as before the save

### Requirement: Undeclared ordered natives fail the test suite
The repository SHALL contain an automated test that enumerates the stdlib extern functions, native emitters and order-sensitive native consumers, and fails when one whose result has an order has no declared order. Extern functions that return only scalars, vectors or other values with no element order SHALL be exempt.

#### Scenario: A new list-returning extern without a declaration
- **WHEN** a contributor adds a stdlib extern function returning `list[entity_id]` and declares no order
- **THEN** the test suite fails and names that function

#### Scenario: A scalar extern needs no declaration
- **WHEN** a stdlib extern function returns `float`
- **THEN** the guard test does not require an order declaration for it
