## Purpose

Define trait declaration modifiers and rule matching qualifiers, including marker (empty-body) traits and the optional `filter:`/`exclude:` blocks that scope rule handlers to matching entities.

## Requirements

### Requirement: Marker (empty-body) traits
The language SHALL allow trait declarations with no body (no colon, no fields, no handlers). A marker trait is a zero-cost tag component used purely for rule filtering. The colon and body block are optional; omitting them produces a valid marker trait declaration.

#### Scenario: Marker trait declared without colon
- **WHEN** `trait Frozen` appears in source (no colon, no indented body)
- **THEN** the parser accepts it as a valid trait declaration with no fields

#### Scenario: Marker trait usable in apply
- **WHEN** a unit or template's `apply:` block lists a marker trait
- **THEN** the semantic analyzer accepts it and includes it in the entity's trait set

#### Scenario: Marker trait usable in filter and exclude
- **WHEN** a rule's `filter:` or `exclude:` block lists a marker trait
- **THEN** the semantic analyzer accepts it; entities are matched/excluded based on whether the marker trait is active

#### Scenario: Marker trait has no accessible fields
- **WHEN** rule handler code attempts to read a field on a marker trait
- **THEN** the compiler SHALL report an error: "trait 'Frozen' has no fields"

#### Scenario: Marker trait can be added and removed dynamically
- **WHEN** `add Invincible` and `remove Invincible` appear in rule handlers
- **THEN** the semantic analyzer accepts both; the trait is attached/detached dynamically

### Requirement: `exclude:` block on rule declarations
Rule declarations SHALL support an optional `exclude:` block containing an indented list of trait names. Entities that have ANY of the listed traits active SHALL be excluded from the rule's processing.

#### Scenario: Entity with excluded trait is skipped
- **WHEN** an entity matches `filter:` but has an active trait listed in `exclude:`
- **THEN** the entity is NOT processed by that rule's handlers

#### Scenario: Entity without excluded trait is processed normally
- **WHEN** an entity matches `filter:` and has NONE of the `exclude:` traits active
- **THEN** the entity IS processed normally

#### Scenario: `exclude:` with add/remove interaction
- **WHEN** a rule has `exclude: Invincible` and an entity gains `Invincible` via `add Invincible`
- **THEN** that entity is excluded from the rule's processing in subsequent handler invocations

#### Scenario: Exclude clause is optional
- **WHEN** a rule has no `exclude:` block
- **THEN** no entities are excluded by trait state (existing behavior)

### Requirement: `filter:` and `exclude:` are both optional; no `filter:` means match all entities
Both `filter:` and `exclude:` SHALL be optional on rule declarations. When `filter:` is omitted, the rule matches all entities. `filter:` and `exclude:` are runtime queries — no static guarantee of component presence is made. The compiler validates that trait names listed in `filter:` and `exclude:` refer to declared traits.

#### Scenario: Rule with no filter matches all entities
- **WHEN** a rule has no `filter:` block
- **THEN** all entities are processed by that rule's handlers (filter_mask = 0)

#### Scenario: Rule with only exclude processed against all entities
- **WHEN** a rule has `exclude: Persistent` but no `filter:`
- **THEN** all entities without `Persistent` currently attached are processed

#### Scenario: Field access without filter rejected
- **WHEN** a rule has no `filter:` block and its handler body accesses a trait field
- **THEN** the compiler SHALL report an error: "trait field not accessible — no filter clause declares this trait"
