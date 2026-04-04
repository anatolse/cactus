## Requirements

### Requirement: Template declaration syntax
The language SHALL support a `template` top-level declaration that defines a reusable entity blueprint using nested trait entries instead of `apply:` and `config:` blocks. A `template` declaration has the same body structure as a `unit` declaration but is NOT automatically instantiated at program start. `template` declarations may be marked `pub` for cross-module access.

#### Scenario: Template declared but not auto-instantiated
- **WHEN** a module contains a `template Foo:` declaration
- **THEN** no entity for `Foo` exists at program start (unlike `unit`)

#### Scenario: Template with pub modifier
- **WHEN** a `template` is declared with `pub`
- **THEN** it is accessible from other modules via qualified name or `use` import

#### Scenario: Template with required fields must initialize them
- **WHEN** a `template` declares traits with required fields and omits those fields from its nested trait blocks
- **THEN** the remaining required fields SHALL be provided at every `spawn` site or the compiler reports an error

### Requirement: `spawn` statement creates entity from template
The language SHALL support a block-structured `spawn` statement inside system event handlers. `spawn TemplateName:` creates a new entity using the named template as its archetype. Nested trait override blocks are merged with the template's trait initializers; provided values take precedence over template values.

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
- **THEN** the compiler SHALL report an error: "`spawn` only allowed inside system event handlers"

### Requirement: `destroy` statement removes current entity
The language SHALL support a `destroy` statement inside system event handlers. `destroy` removes the entity currently being processed by the enclosing handler. Before removal, `on destroy()` lifecycle handlers fire on all systems whose filter matches the entity.

#### Scenario: Destroy removes entity from world
- **WHEN** `destroy` executes inside a system handler
- **THEN** the current entity SHALL be queued for removal and SHALL no longer appear in any system's filter after that frame

#### Scenario: Destroy outside event handler (invalid)
- **WHEN** `destroy` appears outside a system event handler
- **THEN** the compiler SHALL report an error: "`destroy` only allowed inside system event handlers"

#### Scenario: Destroy on persistent entity
- **WHEN** `destroy` is called on an entity that has the `Persistent` trait active
- **THEN** the entity SHALL still be destroyed — `Persistent` only protects against `load`-triggered cleanup

### Requirement: `on spawn()` lifecycle handler on systems
Systems MAY declare an `on spawn():` handler. This handler fires once for each new entity that matches the system's `filter:` (and does not match `exclude:`), after all of the entity's fields have been initialized.

#### Scenario: On spawn fires after fields are initialized
- **WHEN** an entity is created via `spawn` or module `load`
- **THEN** `on spawn()` handlers fire with all trait fields already set to their initial values

#### Scenario: On spawn only for matching entities
- **WHEN** a new entity is created that does NOT match a system's `filter:`
- **THEN** that system's `on spawn()` SHALL NOT fire for that entity

#### Scenario: Multiple systems each receive on spawn
- **WHEN** two systems both have `on spawn()` handlers and a new entity matches both filters
- **THEN** both handlers fire, in the order the systems are declared in source

### Requirement: `on destroy()` lifecycle handler on systems
Systems MAY declare an `on destroy():` handler. This handler fires once for each entity that matches the system's filter and is about to be removed. The handler fires before the entity is actually removed, so trait fields are still accessible.

#### Scenario: On destroy fires before entity removal
- **WHEN** `destroy` is called or a `load` cleans up non-persistent entities
- **THEN** `on destroy()` handlers fire while the entity's fields are still readable
