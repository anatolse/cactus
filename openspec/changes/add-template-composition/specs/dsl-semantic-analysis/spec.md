## ADDED Requirements

### Requirement: Archetype-body template-use entries resolve to templates

The semantic analyzer SHALL resolve every archetype-body `use` entry to a template declaration in the current module or an imported module according to existing name-resolution rules. This is distinct from top-level `use` declarations, which import modules.

#### Scenario: Local template use accepted
- **WHEN** `template WalkerEnemy:` contains `use EnemyBase` and `EnemyBase` is a local template
- **THEN** semantic analysis accepts the archetype-body use

#### Scenario: Use of non-template rejected
- **WHEN** an archetype body contains `use Health` and `Health` resolves to a trait rather than a template
- **THEN** semantic analysis reports that archetype-body template uses must reference templates

#### Scenario: Imported private template rejected
- **WHEN** an archetype body uses a template from another module that is not `pub`
- **THEN** semantic analysis reports that the template is not importable

### Requirement: Template-use graphs are acyclic

The semantic analyzer SHALL reject cycles in template-use graphs.

#### Scenario: Direct template-use cycle rejected
- **WHEN** `template A` uses `B` and `template B` uses `A`
- **THEN** semantic analysis reports a cyclic template-use error with the use chain

#### Scenario: Self use rejected
- **WHEN** `template A` contains `use A`
- **THEN** semantic analysis reports a cyclic template-use error

### Requirement: Used templates flatten deterministically

The semantic analyzer SHALL flatten used template entries in declaration order and merge duplicate trait entries field-by-field, with later entries overriding earlier assignments for the same field.

#### Scenario: Local entry overrides used-template field
- **WHEN** `BossEnemy` uses `EnemyBase` and then defines `Health.health = 50`
- **THEN** the flattened `BossEnemy` archetype uses `Health.health = 50` while preserving other `Health` fields from `EnemyBase`

#### Scenario: Marker duplicates collapse
- **WHEN** two used templates both apply marker trait `Persistent`
- **THEN** the flattened archetype contains one `Persistent` marker entry
