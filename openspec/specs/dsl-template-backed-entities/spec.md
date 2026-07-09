# dsl-template-backed-entities Specification

## Purpose
Define the language semantics for template-backed entity declarations (`entity Name from TemplateName:`), which create module/scene-load entity instances from a named template's flattened archetype with optional nested trait override entries.

## Requirements

### Requirement: Template-backed entities create load-time instances from templates

The language SHALL support template-backed entity declarations using `entity Name from TemplateName:`. A template-backed entity declaration creates one module/scene-load entity from the referenced template's flattened archetype and applies the entity body's nested trait override entries before the entity becomes visible to lifecycle handlers.

#### Scenario: Template-backed entity creates one load-time entity
- **WHEN** a module declares `entity Gem1 from BlueGem:` and `BlueGem` is a valid template
- **THEN** loading the module creates exactly one entity for `Gem1` using `BlueGem` as its base archetype

#### Scenario: Template-backed entity does not run during handler execution
- **WHEN** a module declares `entity Gem1 from BlueGem:`
- **THEN** the declaration contributes to module/scene-load setup and does not execute as a handler statement

### Requirement: Template-backed entity overrides use nested trait blocks

Template-backed entity declarations SHALL use the same nested trait override style as block-structured `spawn` sites. Override entries SHALL merge field-by-field with the referenced template's flattened archetype.

#### Scenario: Override replaces one template field
- **WHEN** `BlueGem` initializes `WorldTransform.position` and `entity Gem1 from BlueGem:` overrides `WorldTransform.position`
- **THEN** the created entity uses the override value for `WorldTransform.position`

#### Scenario: Partial override preserves other template fields
- **WHEN** `BlueGem` initializes `Collectible.point_value` and `Shape.color`, and `entity Gem1 from BlueGem:` overrides only `Shape.color`
- **THEN** the created entity uses the new `Shape.color` and preserves `Collectible.point_value`

### Requirement: Template-backed entity names are declaration identifiers

Template-backed entity names SHALL identify declarations for diagnostics, generated-symbol stability, and future editor tooling. They SHALL NOT introduce author-visible `entity_id` constants in this change.

#### Scenario: Entity declaration name is not an expression
- **WHEN** a module declares `entity Gem1 from BlueGem:` and handler code references `Gem1` as an expression
- **THEN** semantic analysis does not resolve `Gem1` as an `entity_id` solely because of the declaration

### Requirement: Grouped and table-based entity placement is deferred

The active language surface SHALL NOT include grouped declarations such as `entities from TemplateName:` or CSV/table placement import in this change.

#### Scenario: Grouped entities syntax is not accepted
- **WHEN** source contains `entities from BlueGem:`
- **THEN** the parser rejects it as unsupported in this language version

#### Scenario: CSV placement import is not part of core DSL
- **WHEN** authored content needs external editor integration
- **THEN** the core DSL expects generated `.cactus` or future explicit import features rather than CSV placement syntax in this change
