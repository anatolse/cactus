# example-spec-conformance Specification

## Purpose
TBD - created by archiving change canonicalize-supported-examples. Update Purpose after archive.

## Requirements
### Requirement: Maintained examples match current spec and implementation

Maintained `.cactus` examples SHALL demonstrate syntax and semantics accepted by the current parser, semantic analyzer, and implemented backend path when that example is covered by backend compilation.

#### Scenario: Example field access follows semantic rules
- **WHEN** an example mutates or reads a trait field inside a rule handler
- **THEN** the access uses `alias.field` or `TraitName.field` according to the current semantic-analysis rules

#### Scenario: Legacy dynamic trait syntax is not shown as current syntax
- **WHEN** an example demonstrates dynamic trait initialization
- **THEN** it uses block syntax such as `add Invincible:` followed by indented field assignments, not parenthesized named arguments

### Requirement: Examples are fixed in place

The cleanup SHALL preserve existing example locations unless a separate change explicitly justifies moving or reorganizing examples.

#### Scenario: Existing example path remains stable
- **WHEN** an example contains stale syntax or comments
- **THEN** the example is updated in its existing location rather than moved solely for classification purposes

### Requirement: Teaching comments match the implemented surface

Comments in examples SHALL describe the current accepted language surface and SHALL NOT present removed or speculative syntax as implemented.

#### Scenario: Removed syntax is not presented as valid
- **WHEN** an example mentions removed syntax such as `apply:` or `config:`
- **THEN** the comment either identifies it as historical/future-facing context or is removed from the example