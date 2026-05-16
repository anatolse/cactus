## ADDED Requirements

### Requirement: Placement template references resolve to templates

The semantic analyzer SHALL resolve every `place ... from TemplateName` reference to a template declaration using existing local and imported symbol rules.

#### Scenario: Placement from local template accepted
- **WHEN** `place Gem1 from BlueGem:` references a local `template BlueGem`
- **THEN** semantic analysis accepts the placement

#### Scenario: Placement from non-template rejected
- **WHEN** `place Gem1 from Collectible:` references a trait rather than a template
- **THEN** semantic analysis reports that placements must instantiate templates

#### Scenario: Placement from private imported template rejected
- **WHEN** a placement references a non-public template from another module
- **THEN** semantic analysis reports that the template is not importable

### Requirement: Placement overrides merge with template archetype

The semantic analyzer SHALL construct a flattened placement archetype by starting from the referenced template's flattened archetype and applying placement override trait entries field-by-field.

#### Scenario: Placement overrides transform position
- **WHEN** `BlueGem` provides default shape and collectible traits and `place Gem1 from BlueGem:` overrides `WorldTransform.position`
- **THEN** the placement archetype contains the template traits plus the overridden world transform field

#### Scenario: Unknown override field rejected
- **WHEN** a placement override assigns a field that does not exist on the target trait
- **THEN** semantic analysis reports an unknown field error for the placement

### Requirement: Placement names are declaration identifiers, not entity_id constants

Placement declaration names SHALL be used for diagnostics and generated-symbol stability, but SHALL NOT introduce an author-visible `entity_id` constant in v1.

#### Scenario: Placement name is not a value expression
- **WHEN** handler code attempts to use `Gem1` as an expression solely because `place Gem1 from BlueGem:` exists
- **THEN** semantic analysis does not resolve `Gem1` as an `entity_id` value
