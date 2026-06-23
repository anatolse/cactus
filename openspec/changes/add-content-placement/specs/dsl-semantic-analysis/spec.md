## MODIFIED Requirements

### Requirement: Spawn-site validation
At each block-structured `spawn TemplateName:` call site, the semantic analyzer SHALL verify the template exists, every overridden trait exists on the template's flattened archetype, all overridden fields are valid for that trait, and all required fields remain satisfied after applying overrides.

#### Scenario: Spawn of undeclared template rejected
- **WHEN** `spawn UnknownFoo:` is used
- **THEN** the analyzer SHALL report an error: "undefined template 'UnknownFoo'"

#### Scenario: Spawn of entity (not template) rejected
- **WHEN** `spawn Player:` is used and `Player` is an `entity`, not a `template`
- **THEN** the analyzer SHALL report an error: "'Player' is an entity, not a template"

#### Scenario: Spawn overriding trait not present on template rejected
- **WHEN** `spawn Enemy:` contains an override block for `Loot:` but `Enemy` does not define `Loot`
- **THEN** the analyzer SHALL report an error naming the unknown trait override for template `Enemy`

#### Scenario: Spawn with missing required field rejected
- **WHEN** a template has a required field with no default or template initializer and the `spawn` site still leaves it unset
- **THEN** the analyzer SHALL report an error: "required field '<name>' not set for template '<T>'"

## ADDED Requirements

### Requirement: Entity declarations replace unit declarations

The semantic analyzer SHALL treat `entity` declarations as the module/scene-load ECS entity instance construct. Legacy `unit` declarations SHALL NOT be accepted by the active language surface.

#### Scenario: Inline entity accepted
- **WHEN** `entity Player:` contains valid nested trait entries
- **THEN** semantic analysis accepts the entity as one load-time entity instance

#### Scenario: Legacy unit declaration rejected
- **WHEN** a parsed program contains a legacy `unit Player:` declaration
- **THEN** semantic analysis reports that `unit` has been renamed to `entity`

### Requirement: Template-backed entity references resolve to templates

The semantic analyzer SHALL resolve every `entity Name from TemplateName:` reference to a template declaration using existing local and imported symbol rules.

#### Scenario: Template-backed entity from local template accepted
- **WHEN** `entity Gem1 from BlueGem:` references a local `template BlueGem`
- **THEN** semantic analysis accepts the entity

#### Scenario: Template-backed entity from non-template rejected
- **WHEN** `entity Gem1 from Collectible:` references a trait rather than a template
- **THEN** semantic analysis reports that template-backed entities must instantiate templates

#### Scenario: Template-backed entity from private imported template rejected
- **WHEN** an entity references a non-public template from another module
- **THEN** semantic analysis reports that the template is not importable

#### Scenario: Template-backed entity from undefined template rejected
- **WHEN** `entity Gem1 from MissingGem:` references no visible template
- **THEN** semantic analysis reports an undefined template error

### Requirement: Template-backed entity overrides merge with template archetype

The semantic analyzer SHALL construct a flattened entity archetype by starting from the referenced template's flattened archetype and applying the entity declaration's nested trait override entries field-by-field.

#### Scenario: Template-backed entity overrides transform position
- **WHEN** `BlueGem` provides default shape and collectible traits and `entity Gem1 from BlueGem:` overrides `WorldTransform.position`
- **THEN** the entity archetype contains the template traits plus the overridden world transform field

#### Scenario: Template-backed entity partial override preserves template fields
- **WHEN** `Enemy` initializes `Health.max_hp` and `Health.hp`, and `entity Boss from Enemy:` overrides only `Health.hp`
- **THEN** the entity archetype uses the override for `hp` and preserves the template value for `max_hp`

#### Scenario: Unknown override field rejected
- **WHEN** a template-backed entity override assigns a field that does not exist on the target trait
- **THEN** semantic analysis reports an unknown field error for the entity

#### Scenario: Required field may be satisfied by entity override
- **WHEN** a template leaves a required field unset and `entity Gem1 from GemTemplate:` provides that field in an override block
- **THEN** semantic analysis accepts the entity

#### Scenario: Required field missing rejected
- **WHEN** a template leaves a required field unset and `entity Gem1 from GemTemplate:` also omits that field
- **THEN** semantic analysis reports a required field error for the entity

### Requirement: Entity declaration names are not entity_id constants

Entity declaration names SHALL be used for diagnostics and generated-symbol stability, but SHALL NOT introduce author-visible `entity_id` constants in this change.

#### Scenario: Entity name is not a value expression
- **WHEN** handler code attempts to use `Gem1` as an expression solely because `entity Gem1 from BlueGem:` exists
- **THEN** semantic analysis does not resolve `Gem1` as an `entity_id` value

### Requirement: Template-backed entity ordering is deterministic

The semantic analyzer SHALL preserve source declaration order among inline entities and template-backed entities so backends can instantiate them deterministically.

#### Scenario: Mixed entity order preserved
- **WHEN** a module declares `entity A:`, then `entity B from T:`, then `entity C:`
- **THEN** the decorated program records the entities in the source declaration order `A`, `B`, `C`
