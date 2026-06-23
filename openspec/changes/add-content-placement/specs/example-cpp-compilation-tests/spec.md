## ADDED Requirements

### Requirement: Curated examples may validate template-backed entities

After template-backed entity support is implemented, curated example compilation coverage SHALL be able to include examples that use `entity Name from TemplateName:` declarations to instantiate template-backed content.

#### Scenario: Template-backed entity example compiles through backend
- **WHEN** a supported example uses `entity Gem1 from BlueGem:` and targets cpp-entt
- **THEN** curated example compilation coverage generates and compiles backend output for the template-backed entity setup
