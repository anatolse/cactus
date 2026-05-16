## ADDED Requirements

### Requirement: cpp-entt codegen consumes flattened composed archetypes

The cpp-entt backend SHALL generate units and spawned template instances from the semantic analyzer's flattened archetype representation after archetype-body template uses have been resolved and merged.

#### Scenario: Composed unit emits used-template components
- **WHEN** a unit uses `WalkerEnemy` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the generated cpp-entt setup code emplaces components from both templates exactly once according to the flattened archetype

#### Scenario: Spawned composed template emits used-template components
- **WHEN** a handler spawns `WalkerEnemy` and `WalkerEnemy` uses `EnemyBase`
- **THEN** the generated spawn code emplaces all flattened component traits from the composed template
