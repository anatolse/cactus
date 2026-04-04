## REMOVED Requirements

### Requirement: `disabled` initial state in `apply:` block
**Reason**: The `: disabled` annotation is being removed as part of the shift to open-world dynamic traits. The `add`/`remove` statements replace the enable/disable model. The "start inactive" pattern is replaced by simply not listing the trait in `apply:` and using `add` to attach it dynamically when needed.
**Migration**: Remove `: disabled` annotations from all `apply:` blocks. If the trait needs to start absent, simply omit it from `apply:` entirely. Use `add TraitName` or `add TraitName:` when it should be attached.

### Requirement: `enable` and `disable` statements toggle trait activation
**Reason**: `enable`/`disable` are being replaced by `add`/`remove`. The new `remove` statement destroys the component; the "preserve data while invisible to systems" pattern is now expressed using a zero-cost marker trait combined with `exclude:` on systems that should skip those entities.
**Migration**: 
- Replace `disable TraitName` with `add MarkerTrait` (where `MarkerTrait` is a new zero-cost marker used in `exclude:`)
- Replace `enable TraitName` with `remove MarkerTrait`
- If data preservation is critical, keep the data trait always present and use the marker for filtering

## MODIFIED Requirements

### Requirement: Marker (empty-body) traits
The language SHALL allow trait declarations with no body (no colon, no fields, no handlers). A marker trait is a zero-cost tag component used purely for system filtering via `filter:`, `exclude:`, `add`, and `remove`. The colon and body block are optional; omitting them produces a valid marker trait declaration.

#### Scenario: Marker trait declared without colon
- **WHEN** `trait Frozen` appears in source (no colon, no indented body)
- **THEN** the parser accepts it as a valid trait declaration with no fields

#### Scenario: Marker trait usable in apply
- **WHEN** a unit or template's `apply:` block lists a marker trait
- **THEN** the semantic analyzer accepts it and the entity has that trait from spawn time

#### Scenario: Marker trait usable in filter and exclude
- **WHEN** a system's `filter:` or `exclude:` block lists a marker trait
- **THEN** the semantic analyzer accepts it; entities are matched/excluded based on whether the marker trait is currently present (via add/remove)

#### Scenario: Marker trait can be added and removed dynamically
- **WHEN** `add Invincible` and `remove Invincible` appear in system handlers
- **THEN** the semantic analyzer accepts both; the trait is attached/detached dynamically

#### Scenario: Marker trait has no accessible fields
- **WHEN** system handler code attempts to read a field on a marker trait
- **THEN** the compiler SHALL report an error: "trait 'Frozen' has no fields"

### Requirement: `filter:` and `exclude:` are both optional; no `filter:` means match all entities
Both `filter:` and `exclude:` SHALL be optional on system declarations. When `filter:` is omitted, the system matches all entities. `filter:` and `exclude:` are runtime queries — no static guarantee of component presence is made. The compiler validates that trait names listed in `filter:` and `exclude:` refer to declared traits.

#### Scenario: System with no filter matches all entities
- **WHEN** a system has no `filter:` block
- **THEN** all entities are processed by that system's handlers (filter_mask = 0)

#### Scenario: System with only exclude processed against all entities
- **WHEN** a system has `exclude: Persistent` but no `filter:`
- **THEN** all entities without `Persistent` currently attached are processed

#### Scenario: Field access without filter rejected
- **WHEN** a system has no `filter:` block and its handler body accesses a trait field
- **THEN** the compiler SHALL report an error: "trait field not accessible — no filter clause declares this trait"

#### Scenario: `exclude:` with add/remove interaction
- **WHEN** a system has `exclude: Invincible` and an entity gains `Invincible` via `add Invincible`
- **THEN** that entity is excluded from the system's processing in subsequent handler invocations

### Requirement: `exclude:` block on system declarations
System declarations SHALL support an optional `exclude:` block containing an indented list of trait names. Entities that currently have ANY of the listed traits (i.e., the trait has been added and not removed) SHALL be excluded from the system's processing.

#### Scenario: Entity with excluded trait is skipped
- **WHEN** an entity has trait `Invincible` currently attached and a system has `exclude: Invincible`
- **THEN** the entity is NOT processed by that system's handlers

#### Scenario: Entity without excluded trait is processed normally
- **WHEN** an entity does not currently have `Invincible` and a system has `exclude: Invincible`
- **THEN** the entity IS processed normally

#### Scenario: Exclude clause is optional
- **WHEN** a system has no `exclude:` block
- **THEN** no entities are excluded by trait state (existing behavior)
