## MODIFIED Requirements

### Requirement: Event validation
The semantic analyzer SHALL verify that all `emit` statements reference declared events. For block-structured `emit`, each payload field assignment SHALL correspond to a declared field on the resolved event type. All event handlers SHALL have no parameter list.

#### Scenario: Emit block with declared fields accepted
- **WHEN** a system handler contains `emit Damage:` with payload assignments for fields declared on `event Damage:`
- **THEN** the analyzer accepts the emit statement

#### Scenario: Emit block with unknown payload field rejected
- **WHEN** a system handler contains `emit Damage:` with a payload assignment for a field not declared on `event Damage:`
- **THEN** the analyzer reports an error naming the unknown event field

#### Scenario: Emit of undeclared event rejected
- **WHEN** a system handler contains `emit Foo:` and no `event Foo:` is declared
- **THEN** the analyzer reports an error "undeclared event 'Foo'"

### Requirement: Template declaration validation
The semantic analyzer SHALL validate `template` declarations by checking that every nested trait entry names a declared trait and that every field assignment inside a trait block belongs to that trait.

#### Scenario: Template with undeclared trait rejected
- **WHEN** a `template Foo:` contains a nested trait entry `UnknownTrait:`
- **THEN** the analyzer SHALL report an error: "undeclared trait 'UnknownTrait'"

#### Scenario: Template with invalid nested field rejected
- **WHEN** a `template` assigns a field inside `Health:` that is not declared on `Health`
- **THEN** the analyzer SHALL report an error naming the unknown trait field

### Requirement: Spawn-site validation
At each block-structured `spawn TemplateName:` call site, the semantic analyzer SHALL verify the template exists, every overridden trait exists on the template, all overridden fields are valid for that trait, and all required fields remain satisfied after applying overrides.

#### Scenario: Spawn of undeclared template rejected
- **WHEN** `spawn UnknownFoo:` is used
- **THEN** the analyzer SHALL report an error: "undefined template 'UnknownFoo'"

#### Scenario: Spawn of unit (not template) rejected
- **WHEN** `spawn Player:` is used and `Player` is a `unit`, not a `template`
- **THEN** the analyzer SHALL report an error: "'Player' is a unit, not a template"

#### Scenario: Spawn overriding trait not present on template rejected
- **WHEN** `spawn Enemy:` contains an override block for `Loot:` but `Enemy` does not define `Loot`
- **THEN** the analyzer SHALL report an error naming the unknown trait override for template `Enemy`

#### Scenario: Spawn with missing required field rejected
- **WHEN** a template has a required field with no default or template initializer and the `spawn` site still leaves it unset
- **THEN** the analyzer SHALL report an error: "required field '<name>' not set for template '<T>'"

### Requirement: Targeted emit validation
The semantic analyzer SHALL verify that the expression in an `emit EventName to expression:` statement evaluates to type `entity_id`.

#### Scenario: Targeted emit with entity_id field accepted
- **WHEN** `emit Damage to EnemyAI.target:` is used and `EnemyAI.target` is of type `entity_id`
- **THEN** the analyzer accepts the targeted emit

#### Scenario: Targeted emit with non-entity_id expression rejected
- **WHEN** `emit Damage to Position.x:` is used and `Position.x` is of type `float`
- **THEN** the analyzer reports an error: "emit target must be of type entity_id, got float"

## REMOVED Requirements

### Requirement: `apply:` alias uniqueness validation
**Reason**: Archetype declarations no longer support `apply:` aliases.
**Migration**: Replace alias-based archetype configuration with nested trait blocks.

### Requirement: Qualified `config:` key resolution
**Reason**: `config:` blocks are removed. Trait ownership is explicit in the nested syntax, so key qualification rules are unnecessary.
**Migration**: Move field assignments into the owning trait block in the unit or template body.

### Requirement: Qualified `spawn()` override argument key resolution
**Reason**: `spawn` no longer uses flat override arguments. Nested trait override blocks replace prefixed key resolution.
**Migration**: Move spawn override fields into the appropriate nested trait block under `spawn TemplateName:`.