## Requirements

### Requirement: Marker (empty-body) traits
The language SHALL allow trait declarations with no body (no colon, no fields, no handlers). A marker trait is a zero-cost tag component used purely for system filtering. The colon and body block are optional; omitting them produces a valid marker trait declaration.

#### Scenario: Marker trait declared without colon
- **WHEN** `trait Frozen` appears in source (no colon, no indented body)
- **THEN** the parser accepts it as a valid trait declaration with no fields

#### Scenario: Marker trait usable in apply
- **WHEN** a unit or template's `apply:` block lists a marker trait
- **THEN** the semantic analyzer accepts it and includes it in the entity's trait set

#### Scenario: Marker trait usable in filter and exclude
- **WHEN** a system's `filter:` or `exclude:` block lists a marker trait
- **THEN** the semantic analyzer accepts it; entities are matched/excluded based on whether the marker trait is active

#### Scenario: Marker trait has no accessible fields
- **WHEN** system handler code attempts to read a field on a marker trait
- **THEN** the compiler SHALL report an error: "trait 'Frozen' has no fields"

### Requirement: `disabled` initial state in `apply:` block
Within an `apply:` block, a trait entry MAY be followed by `: disabled` to indicate that the trait starts in an inactive state. A disabled trait's data is still allocated; it is simply excluded from system filter matching until `enable`d.

#### Scenario: Trait marked disabled starts inactive
- **WHEN** an entity is created from a template/unit with `Frozen: disabled`
- **THEN** `Frozen` is inactive on that entity; systems with `filter:` listing `Frozen` do NOT process it

#### Scenario: Trait without disabled annotation starts active
- **WHEN** an entity is created from a template/unit with `Position` (no `: disabled`)
- **THEN** `Position` is active on that entity by default

### Requirement: `enable` and `disable` statements toggle trait activation
The language SHALL support `enable TraitName` and `disable TraitName` statements inside system event handlers. These statements activate or deactivate the named trait on the entity currently being processed. Trait field data is preserved through enable/disable cycles.

#### Scenario: Disable removes entity from matching filter
- **WHEN** `disable EnemyAI` executes on an entity
- **THEN** that entity no longer matches any system whose `filter:` includes `EnemyAI`

#### Scenario: Enable restores entity to matching filter
- **WHEN** `enable EnemyAI` executes on an entity that had `EnemyAI` disabled
- **THEN** that entity again matches systems whose `filter:` includes `EnemyAI`

#### Scenario: Field data preserved through disable/enable cycle
- **WHEN** `disable EnemyAI` then `enable EnemyAI` are called
- **THEN** all fields of `EnemyAI` retain their values from before the disable

#### Scenario: Enable/disable on trait not in apply (invalid)
- **WHEN** `enable Frozen` is called inside a system whose filter entities don't have `Frozen` in their `apply:` block
- **THEN** the semantic analyzer SHALL report an error: "trait 'Frozen' is not in the apply block of entities matching this system's filter"

#### Scenario: Enable/disable only affects current entity
- **WHEN** `disable Frozen` executes inside a system handler
- **THEN** only the entity currently being processed by that handler is affected

#### Scenario: Enable/disable outside event handler (invalid)
- **WHEN** `enable` or `disable` appears outside a system event handler
- **THEN** the compiler SHALL report an error: "`enable`/`disable` only allowed inside system event handlers"

### Requirement: `exclude:` block on system declarations
System declarations SHALL support an optional `exclude:` block containing an indented list of trait names. Entities that have ANY of the listed traits active SHALL be excluded from the system's processing.

#### Scenario: Entity with excluded trait is skipped
- **WHEN** an entity matches `filter:` but has an active trait listed in `exclude:`
- **THEN** the entity is NOT processed by that system's handlers

#### Scenario: Entity without excluded trait is processed normally
- **WHEN** an entity matches `filter:` and has NONE of the `exclude:` traits active
- **THEN** the entity IS processed normally

#### Scenario: Exclude with disabled trait — entity is included
- **WHEN** an entity has `Frozen` in its `apply:` but `Frozen` is currently disabled
- **THEN** a system with `exclude: Frozen` DOES process that entity (the trait is inactive)

#### Scenario: Exclude clause is optional
- **WHEN** a system has no `exclude:` block
- **THEN** no entities are excluded by trait state (existing behavior)

### Requirement: `filter:` and `exclude:` are both optional; no `filter:` means match all entities
Both `filter:` and `exclude:` are optional on system declarations. When `filter:` is omitted, the system matches all entities. A system with no `filter:` cannot read or write any trait fields — only lifecycle operations are valid.

#### Scenario: System with no filter matches all entities
- **WHEN** a system has no `filter:` block
- **THEN** all entities are processed by that system's handlers (filter_mask = 0)

#### Scenario: System with only exclude processed against all entities
- **WHEN** a system has `exclude: Persistent` but no `filter:`
- **THEN** all entities without `Persistent` active are processed

#### Scenario: Field access without filter rejected
- **WHEN** a system has no `filter:` block and its handler body accesses a trait field
- **THEN** the compiler SHALL report an error: "trait field not accessible — no filter clause declares this trait"
