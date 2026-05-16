## ADDED Requirements

### Requirement: cpp-entt setup instantiates placements

The cpp-entt backend SHALL instantiate each analyzed placement during generated module/world setup using the flattened placement archetype.

#### Scenario: Placement emits template components plus overrides
- **WHEN** `place Gem1 from BlueGem:` overrides `WorldTransform.position`
- **THEN** generated setup code creates one entity with all components from `BlueGem` and the overridden `WorldTransform.position` value

### Requirement: Unit and placement creation order is deterministic

The cpp-entt backend SHALL preserve source declaration order among units and placements within a module when generating setup code.

#### Scenario: Mixed unit and placement order preserved
- **WHEN** a source module declares `unit A`, then `place B from T`, then `unit C`
- **THEN** generated setup code creates A before B and B before C
