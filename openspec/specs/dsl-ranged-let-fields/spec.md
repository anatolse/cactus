# dsl-ranged-let-fields Specification

## Purpose

Lets a trait state that an integer `let` field lies in a range bounded by constants or by fields of a container entity, so the compiler can check the fact, the runtime can enforce it, and the backend can derive grid-shaped data structures from it.

## Requirements

### Requirement: A `let` int field may declare a half-open range
A trait field declared `let` with type `int` MAY be followed by `in range(a, b)`. The range SHALL be half-open with the same meaning as the loop `range`: a value `v` is in range when `a <= v` and `v < b`. A range on a `var` field, on a field of any other type, or with a `step` argument SHALL be a compile error that names the field. A field SHALL declare at most one range.

#### Scenario: A ranged coordinate
- **WHEN** a trait declares `let x: int in range(0, 16)`
- **THEN** compilation succeeds and `x` accepts the values 0 through 15

#### Scenario: A range on a var field is rejected
- **WHEN** a trait declares `var x: int in range(0, 16)`
- **THEN** compilation fails with a diagnostic naming `x` that says a range needs a `let` field

#### Scenario: A range on a float field is rejected
- **WHEN** a trait declares `let t: float in range(0, 1)`
- **THEN** compilation fails with a diagnostic naming `t` that says a range needs an `int` field

### Requirement: Range bounds read only values fixed for the field's lifetime
Each bound SHALL be an `int` expression that reads only literals, constants, `let` fields of the same trait, and container reads `f[T].g`, where `f` is a `let entity_id` field of the same trait and `g` is a `let` field of trait `T`. Operators SHALL be allowed; calls, `var` fields, named entities, and reads through any other path SHALL be compile errors naming the bound. Each trait `T` read this way SHALL be a *container trait* of the range.

#### Scenario: Bounds read a container's let fields
- **WHEN** trait `Cell` declares `let volume: entity_id` and `let x: int in range(volume[Volume].min_x, volume[Volume].max_x + 1)`, and `Volume.min_x` and `Volume.max_x` are `let` fields
- **THEN** compilation succeeds and `Volume` is a container trait of the range

#### Scenario: A bound reading a var field is rejected
- **WHEN** a bound reads `volume[Volume].max_x` and `max_x` is a `var` field
- **THEN** compilation fails with a diagnostic naming the bound and `Volume.max_x`

#### Scenario: A bound through a var handle is rejected
- **WHEN** a bound reads `owner[Volume].max_x` and `owner` is a `var entity_id` field
- **THEN** compilation fails with a diagnostic naming `owner`

### Requirement: A container trait is given only when its entity is made
A container trait SHALL get its value only when its entity is made: from an entity or template body, a `spawn` override block, or a `children:` override block. An `add T:` or `project T:` of a container trait anywhere in the program, including an extern rule's `add` capability, SHALL be a compile error that names the trait and a ranged field that reads it. Removing a container trait or destroying its entity SHALL remain allowed and SHALL NOT change values already made.

#### Scenario: Adding a container trait is rejected
- **WHEN** `Volume` is a container trait and a handler runs `add Volume: max_x = 8` on some entity
- **THEN** compilation fails with a diagnostic naming `Volume` and the ranged field that reads it

#### Scenario: Destroying a container keeps its cells
- **WHEN** a volume entity is destroyed while cells ranged against it exist
- **THEN** those cells keep their values and remain selectable

### Requirement: A ranged value is checked when its trait value is made
Whenever a value of a trait with ranged fields is made — when declared entities load, when a `spawn` command applies at commit, when an `add T:` command applies, when a `project T:` block runs, and when a document is restored — each ranged field SHALL be checked at that point, after its bounds are evaluated against the world state at the command's position. The check SHALL fail when a container handle is stale or names an entity that does not carry the container trait, or when the value is outside the range. A failed check SHALL make the whole operation do nothing: a spawn creates no entity and no children, so its handle stays stale and no lifecycle trigger fires; an `add` leaves the entity as it was; a `project` leaves the trait unprojected; a declared entity is not created and its named handle is stale. The runtime SHALL report each failure with the trait, the field, the value and the evaluated range. Restore SHALL check a ranged value only when its container handle refers to an entity in the restored document; a value whose container handle is stale in the document is restored unchanged, as it was valid when made. A restore whose document holds a value that fails the check SHALL fail as invalid data and keep the current world.

#### Scenario: An out-of-range spawn creates nothing
- **WHEN** a handler spawns a cell with `x = 16` against a volume whose `max_x` is 15
- **THEN** after the commit no entity exists for that spawn, its handle is stale, no `on added` trigger fires, and the runtime reports the cell's `x`, the value 16 and the range 0..16

#### Scenario: A spawn against a stale container creates nothing
- **WHEN** a handler spawns a cell whose `volume` handle names a destroyed entity
- **THEN** after the commit no entity exists for that spawn and the runtime reports the failure

#### Scenario: Checks follow command order
- **WHEN** one commit first spawns a volume and then, later in command order, spawns a cell ranged against it
- **THEN** the cell's check sees the volume and the cell is created

#### Scenario: A restored document out of range is rejected
- **WHEN** a restored document holds a cell whose `x` is outside its volume's range
- **THEN** the restore fails as invalid data and the current world is unchanged

#### Scenario: Cells of a destroyed volume survive save and load
- **WHEN** a world whose volume was destroyed, while its cells remain, is saved and restored
- **THEN** the restore succeeds and the cells keep their coordinates

#### Scenario: In-range values behave as before
- **WHEN** every made value is inside its range
- **THEN** the program's observable behaviour is the same as if the fields declared no ranges
