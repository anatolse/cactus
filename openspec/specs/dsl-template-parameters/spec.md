## Purpose

Define typed template interfaces, argument binding, static composition, and creation-time evaluation. This capability gives gameplay authors predictable behavior without backend plumbing.

## Requirements

### Requirement: Typed named template interfaces

Templates SHALL accept typed immutable parameters with named arguments in entity-from, spawn, archetype-use, and explicit child entity template references. Omitting a parameter list SHALL preserve parameterless behavior. Parameter defaults SHALL be pure expressions of constants and earlier parameters; required, unknown, duplicate, wrong-type and positional arguments SHALL produce source-located diagnostics.

#### Scenario: Named application
- **WHEN** Bullet(origin = p, radius = 4.0) is applied in a spawn site
- **THEN** origin and radius bind only declared parameters, not arbitrary trait fields

#### Scenario: Invalid arguments accumulate diagnostics
- **WHEN** separate applications contain missing, duplicate and wrong-type arguments
- **THEN** the compiler reports each error without abandoning analysis of unrelated declarations

#### Scenario: Dependent default
- **WHEN** velocity defaults to vec2(speed, 0.0) and speed is supplied as 12.0
- **THEN** velocity uses 12.0; references to later parameters are rejected

### Requirement: Single evaluation and static composition

Every application's explicit arguments SHALL be evaluated exactly once in source order, followed by omitted defaults in parameter declaration order. Root and descendant initializers SHALL reuse bound values. Template expansion SHALL fix all traits and children statically, retain existing hierarchy creation/commit timing, and apply explicit override bodies after parameter substitution.

#### Scenario: Reuse across hierarchy
- **WHEN** one argument is referenced by the root and two child initializers
- **THEN** all three use the same evaluated value

#### Scenario: Override precedence
- **WHEN** an application supplies radius and explicitly overrides one derived visual field
- **THEN** that field uses the override and unrelated parameter-derived fields retain their values

#### Scenario: No new structural visibility
- **WHEN** a parameterized tree is spawned within an activation
- **THEN** its structural visibility follows the same commit barriers as a parameterless tree

### Requirement: Application context and compatibility

Argument expressions SHALL be pure and valid at their creation site. Spawn arguments SHALL accept handler locals, trait values and event/phase data; load-time entity arguments SHALL NOT capture handler state. Public interfaces SHALL work across source and module-artifact imports. Parameterized applications MAY omit a colon/body; explicit child syntax SHALL remain sufficient without the child-shorthand draft.

#### Scenario: Cross-module template
- **WHEN** an imported public template is instantiated from a compiled module artifact
- **THEN** binding and results match a source import

#### Scenario: Existing program
- **WHEN** a parameterless entity, spawn or use is compiled
- **THEN** existing behavior remains unchanged

#### Scenario: Static structure restriction
- **WHEN** a template recursively expands or attempts to choose traits or children from a parameter value
- **THEN** compilation rejects it with the application path

#### Scenario: Template is not a value
- **WHEN** a template application appears as an ordinary value expression outside a creation reference
- **THEN** compilation rejects it
