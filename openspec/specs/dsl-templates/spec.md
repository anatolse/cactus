## Purpose

Define the `template` declaration and its runtime counterparts, including composition semantics, the `spawn`/`destroy` statements, and load-time instantiation of template-backed entities.
## Requirements
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

### Requirement: `spawn` statement creates entity from template
The language SHALL support a block-structured `spawn` statement inside rule event handlers. `spawn TemplateName:` creates a new entity using the named template's already-composed archetype. Nested trait override blocks are merged with the template's flattened trait initializers; provided values take precedence over template values.

#### Scenario: Spawn can override defaulted field
- **WHEN** `spawn Foo:` overrides a field that `Foo` already initializes in its template body
- **THEN** the spawn-site value takes precedence

#### Scenario: Spawn with partial overrides keeps remaining template values
- **WHEN** `spawn Foo:` overrides one field on a trait and `Foo` initializes other fields on the same or other traits
- **THEN** the new entity uses the override for the provided field and keeps the remaining template-initialized values

#### Scenario: Spawn with unknown trait field name
- **WHEN** `spawn Foo:` assigns a field not declared on the named overridden trait
- **THEN** the compiler SHALL report an error naming the unknown field for that trait on template `Foo`

#### Scenario: Spawn with missing required field
- **WHEN** `spawn Foo:` omits a field that has no template initializer or trait default and is still required
- **THEN** the compiler SHALL report an error: "required field '<name>' not set for template 'Foo'"

#### Scenario: Spawn outside event handler (invalid)
- **WHEN** `spawn` appears at module top-level or inside a `func` body
- **THEN** the compiler SHALL report an error: "`spawn` only allowed inside rule event handlers"

### Requirement: `destroy` statement removes current entity
The language SHALL support a `destroy` statement inside rule event handlers. `destroy` removes the entity currently being processed by the enclosing handler. Removal is buffered to the activation commit; no handler fires for the destroyed entity. To react to an entity's end, a rule adds a marker trait (for example `Dying`), reacts with `on added Dying`, and destroys the entity afterwards.

#### Scenario: Destroy removes entity from world
- **WHEN** `destroy` executes inside a rule handler
- **THEN** the current entity SHALL be queued for removal and SHALL no longer appear in any rule's filter after that frame

#### Scenario: Destroy outside event handler (invalid)
- **WHEN** `destroy` appears outside a rule event handler
- **THEN** the compiler SHALL report an error: "`destroy` only allowed inside rule event handlers"

#### Scenario: Destroy on persistent entity
- **WHEN** `destroy` is called on an entity that has the `KeepOnLoad` trait attached
- **THEN** the entity SHALL still be destroyed — `KeepOnLoad` only protects against `load`-triggered cleanup

#### Scenario: Destroy fires no handler
- **WHEN** an entity carrying `Burning` is destroyed, and a rule declares `on removed Burning`
- **THEN** no handler runs for the destroyed entity

### Requirement: Template-backed entities instantiate composed templates at load time

The language SHALL support `entity Name from TemplateName:` as the load-time counterpart to runtime `spawn TemplateName:`. A template-backed entity creates one module/scene-load entity from the named template's already-composed archetype and applies nested trait override blocks before lifecycle delivery.

#### Scenario: Template-backed entity uses composed template
- **WHEN** `entity FirstWalker from WalkerEnemy:` appears and `WalkerEnemy` uses `EnemyBase`
- **THEN** the load-time entity is created from the flattened `WalkerEnemy` archetype and receives trait initializers from both `WalkerEnemy` and `EnemyBase`

#### Scenario: Template-backed entity is distinct from spawn
- **WHEN** `entity FirstWalker from WalkerEnemy:` is declared at the top level
- **THEN** the entity is created during module/scene load rather than during handler execution, and no `entity_id` expression is produced at the declaration site

### Requirement: `spawn` override values are evaluated when the statement runs
Every field value expression in a `spawn` statement's override blocks, including nested `children:` overrides, SHALL be evaluated exactly once, in source order, when the `spawn` statement executes, even though entity creation applies later at the activation commit. Later changes in the same activation to anything those expressions read SHALL NOT change the values the new entity receives. This SHALL hold equally for trait field reads, handler locals, and calls to extern functions that read world state.

#### Scenario: Trait read and extern call see the same moment
- **WHEN** a handler runs `spawn Spawned:` with a `SpawnResult:` override block setting `from_field = transform.position.x` and `from_extern = tv.world_position(self).x` while position x is 1.0, then assigns position x = 99.0 in the same handler
- **THEN** after the commit the spawned entity's `SpawnResult.from_field` and `SpawnResult.from_extern` both equal 1.0

#### Scenario: Child override is evaluated at the statement
- **WHEN** a `spawn` statement's `children:` override sets a child field from `tv.world_position(self).x` and the handler later moves `self`
- **THEN** the created child receives the position read when the `spawn` statement ran

