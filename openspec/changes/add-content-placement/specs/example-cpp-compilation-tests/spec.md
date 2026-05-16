## ADDED Requirements

### Requirement: Curated examples may validate placement-backed content

After placement support is implemented, curated example compilation coverage SHALL be able to include examples that use `place` declarations to instantiate template-backed content.

#### Scenario: Placement example compiles through backend
- **WHEN** a supported example uses `place Gem1 from BlueGem:` and targets cpp-entt
- **THEN** curated example compilation coverage generates and compiles backend output for the placement-backed entity setup
