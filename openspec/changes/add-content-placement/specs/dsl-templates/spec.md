## MODIFIED Requirements

### Requirement: Template declaration syntax
The language SHALL support a `template` top-level declaration that defines a reusable entity blueprint using an archetype body instead of `apply:` and `config:` blocks. An archetype body contains nested trait entries and MAY contain body-level `use TemplateName` entries that compose another template at compile time. A `template` declaration has the same body structure as an inline `entity` declaration but is NOT automatically instantiated at program start. `template` declarations may be marked `pub` for cross-module access.

#### Scenario: Template declared but not auto-instantiated
- **WHEN** a module contains a `template Foo:` declaration
- **THEN** no entity for `Foo` exists at program start (unlike `entity Foo:`)

#### Scenario: Template with pub modifier
- **WHEN** a `template` is declared with `pub`
- **THEN** it is accessible from other modules via qualified name or `use` import

#### Scenario: Template with required fields must initialize them
- **WHEN** a `template` declares traits with required fields and omits those fields from its nested trait blocks
- **THEN** the remaining required fields SHALL be provided at every `spawn` site or template-backed `entity` declaration, or the compiler reports an error

#### Scenario: Template composes another template
- **WHEN** `template WalkerEnemy:` contains an archetype-body entry `use EnemyBase`
- **THEN** `WalkerEnemy` is a composed blueprint whose flattened trait initializers include `EnemyBase` and its own entries without creating an entity

### Requirement: Template composition is distinct from runtime spawn
Archetype-body `use TemplateName` SHALL be compile-time blueprint composition. It SHALL NOT instantiate an entity, return an `entity_id`, or fire lifecycle handlers. `spawn TemplateName:` SHALL be the runtime construct that creates an entity from the named template after composition has been flattened.

#### Scenario: Entity uses template without spawning
- **WHEN** `entity FirstWalker:` contains `use WalkerEnemy`
- **THEN** the entity is instantiated as one entity with `WalkerEnemy`'s flattened trait initializers and no additional entity is spawned by the `use` entry

#### Scenario: Runtime spawn uses composed template
- **WHEN** a handler executes `spawn WalkerEnemy:` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the spawned entity is created from the flattened `WalkerEnemy` archetype and receives trait initializers from both `WalkerEnemy` and `EnemyBase`

## ADDED Requirements

### Requirement: Template-backed entities instantiate composed templates at load time

The language SHALL support `entity Name from TemplateName:` as the load-time counterpart to runtime `spawn TemplateName:`. A template-backed entity creates one module/scene-load entity from the named template's already-composed archetype and applies nested trait override blocks before lifecycle delivery.

#### Scenario: Template-backed entity uses composed template
- **WHEN** `entity FirstWalker from WalkerEnemy:` appears and `WalkerEnemy` uses `EnemyBase`
- **THEN** the load-time entity is created from the flattened `WalkerEnemy` archetype and receives trait initializers from both `WalkerEnemy` and `EnemyBase`

#### Scenario: Template-backed entity is distinct from spawn
- **WHEN** `entity FirstWalker from WalkerEnemy:` is declared at the top level
- **THEN** the entity is created during module/scene load rather than during handler execution, and no `entity_id` expression is produced at the declaration site
