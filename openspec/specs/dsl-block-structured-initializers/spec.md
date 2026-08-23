## Purpose

Define the nested block-structured initializer syntax used for archetype, `spawn`, and `emit` expressions.

## Requirements

### Requirement: Nested block initialization syntax for archetypes, spawn, and emit
The language SHALL support a unified block-structured initialization style in three contexts:

- `unit` and `template` declarations use nested trait entries
- `spawn` uses nested trait override entries
- `emit` uses a nested event payload field block

This syntax SHALL express structural ownership directly in the source instead of relying on flat prefix-qualified assignment keys.

#### Scenario: Unit uses nested trait entries
- **WHEN** a unit body contains `Position:` followed by indented field assignments and also contains a bare marker trait entry `Persistent`
- **THEN** the compiler interprets the body as an archetype with one data trait and one marker trait

#### Scenario: Spawn uses nested trait override entries
- **WHEN** a handler contains `spawn Enemy:` followed by `Position:` and indented field assignments
- **THEN** the compiler interprets the spawn site as overriding the `Position` trait fields for the `Enemy` template instance

#### Scenario: Emit uses nested payload fields
- **WHEN** a handler contains `emit Damage:` followed by indented field assignments
- **THEN** the compiler interprets the block as named initialization of the `Damage` event payload