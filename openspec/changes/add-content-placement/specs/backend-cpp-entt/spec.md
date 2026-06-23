## MODIFIED Requirements

### Requirement: Entity creation from units
The backend SHALL generate entity creation code from `entity` declarations. Each nested trait entry SHALL be added as a component to the created entity with authored or default field values.

#### Scenario: Entity with nested trait config
- **WHEN** the decorated AST contains `entity Cactus:` with nested `Position:` and `Renderable:` trait entries
- **THEN** the backend generates `auto entity = registry.create();` followed by `registry.emplace<Position>(entity, ...);` and `registry.emplace<Renderable>(entity, ...);`

### Requirement: cpp-entt codegen consumes flattened composed archetypes
The cpp-entt backend SHALL generate inline entities, template-backed entities, and spawned template instances from the semantic analyzer's flattened archetype representation after archetype-body template uses have been resolved and merged.

#### Scenario: Composed entity emits used-template components
- **WHEN** an inline entity uses `WalkerEnemy` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the generated cpp-entt setup code emplaces components from both templates exactly once according to the flattened archetype

#### Scenario: Spawned composed template emits used-template components
- **WHEN** a handler spawns `WalkerEnemy` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the generated spawn code emplaces all flattened component traits from the composed template

## ADDED Requirements

### Requirement: cpp-entt setup instantiates template-backed entities

The cpp-entt backend SHALL instantiate each analyzed `entity Name from TemplateName:` declaration during generated module/world setup using the flattened template-backed entity archetype.

#### Scenario: Template-backed entity emits template components plus overrides
- **WHEN** `entity Gem1 from BlueGem:` overrides `WorldTransform.position`
- **THEN** generated setup code creates one entity with all components from `BlueGem` and the overridden `WorldTransform.position` value

### Requirement: Inline and template-backed entity creation order is deterministic

The cpp-entt backend SHALL preserve source declaration order among inline entities and template-backed entities within a module when generating setup code.

#### Scenario: Mixed entity order preserved
- **WHEN** a source module declares `entity A:`, then `entity B from T:`, then `entity C:`
- **THEN** generated setup code creates A before B and B before C
