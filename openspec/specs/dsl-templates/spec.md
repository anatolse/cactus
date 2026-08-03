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
The language SHALL support a `destroy` statement inside rule event handlers. `destroy` removes the entity currently being processed by the enclosing handler. Before removal, `on destroy()` lifecycle handlers fire on all rules whose filter matches the entity.

#### Scenario: Destroy removes entity from world
- **WHEN** `destroy` executes inside a rule handler
- **THEN** the current entity SHALL be queued for removal and SHALL no longer appear in any rule's filter after that frame

#### Scenario: Destroy outside event handler (invalid)
- **WHEN** `destroy` appears outside a rule event handler
- **THEN** the compiler SHALL report an error: "`destroy` only allowed inside rule event handlers"

#### Scenario: Destroy on persistent entity
- **WHEN** `destroy` is called on an entity that has the `Persistent` trait active
- **THEN** the entity SHALL still be destroyed — `Persistent` only protects against `load`-triggered cleanup

### Requirement: `on spawn()` lifecycle handler on rules
Rules MAY declare an `on spawn():` handler. This handler fires once for each new entity that matches the rule's `filter:` (and does not match `exclude:`), after all of the entity's fields have been initialized.

#### Scenario: On spawn fires after fields are initialized
- **WHEN** an entity is created via `spawn` or module `load`
- **THEN** `on spawn()` handlers fire with all trait fields already set to their initial values

#### Scenario: On spawn only for matching entities
- **WHEN** a new entity is created that does NOT match a rule's `filter:`
- **THEN** that rule's `on spawn()` SHALL NOT fire for that entity

#### Scenario: Multiple rules each receive on spawn
- **WHEN** two rules both have `on spawn()` handlers and a new entity matches both filters
- **THEN** both handlers fire, in the order the rules are declared in source

### Requirement: `on destroy()` lifecycle handler on rules
Rules MAY declare an `on destroy():` handler. This handler fires once for each entity that matches the rule's filter and is about to be removed. The handler fires before the entity is actually removed, so trait fields are still accessible.

#### Scenario: On destroy fires before entity removal
- **WHEN** `destroy` is called or a `load` cleans up non-persistent entities
- **THEN** `on destroy()` handlers fire while the entity's fields are still readable

### Requirement: Template-backed entities instantiate composed templates at load time

The language SHALL support `entity Name from TemplateName:` as the load-time counterpart to runtime `spawn TemplateName:`. A template-backed entity creates one module/scene-load entity from the named template's already-composed archetype and applies nested trait override blocks before lifecycle delivery.

#### Scenario: Template-backed entity uses composed template
- **WHEN** `entity FirstWalker from WalkerEnemy:` appears and `WalkerEnemy` uses `EnemyBase`
- **THEN** the load-time entity is created from the flattened `WalkerEnemy` archetype and receives trait initializers from both `WalkerEnemy` and `EnemyBase`

#### Scenario: Template-backed entity is distinct from spawn
- **WHEN** `entity FirstWalker from WalkerEnemy:` is declared at the top level
- **THEN** the entity is created during module/scene load rather than during handler execution, and no `entity_id` expression is produced at the declaration site
