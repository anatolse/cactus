## MODIFIED Requirements

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

#### Scenario: Spawn outside event handler invalid
- **WHEN** `spawn` appears at module top-level or inside a `func` body
- **THEN** the compiler SHALL report an error: "`spawn` only allowed inside system event handlers"