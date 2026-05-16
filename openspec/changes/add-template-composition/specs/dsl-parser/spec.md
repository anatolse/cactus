## ADDED Requirements

### Requirement: Archetype bodies support template-use entries

The parser SHALL accept archetype-body `use TemplateName` entries in archetype bodies that otherwise contain nested trait entries. Top-level `use` declarations SHALL retain their existing module-import meaning; `use` inside a `template` or `unit` body SHALL be parsed as template composition.

```ebnf
archetype_entry    = template_use_entry | archetype_trait_entry ;
template_use_entry = "use" dotted_name NEWLINE ;
```

#### Scenario: Template uses another template
- **WHEN** source contains `template WalkerEnemy:` with an indented `use EnemyBase` entry
- **THEN** the parser produces a template declaration containing an archetype template-use entry referencing `EnemyBase`

#### Scenario: Unit uses a template
- **WHEN** source contains `unit Walker1:` with an indented `use WalkerEnemy` entry
- **THEN** the parser produces a unit declaration containing an archetype template-use entry referencing `WalkerEnemy`

#### Scenario: Qualified archetype use parsed
- **WHEN** source contains `use enemies.WalkerEnemy` inside an archetype body
- **THEN** the parser records the used template name as a dotted name

#### Scenario: Top-level use remains module import
- **WHEN** source contains `use std.physics.flat as phys` at the top level
- **THEN** the parser produces the existing module import node rather than an archetype template-use entry
