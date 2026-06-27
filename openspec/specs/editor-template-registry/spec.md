## Purpose
Define the codegen-emitted template registry that maps `pub template` names to factory functions, enabling runtime template spawning by name from the editor.

## Requirements

### Requirement: Codegen emits a template registry for pub template declarations
After emitting all `create_X` factory functions, the codegen SHALL emit a static `cactus_template_registry` map. The map type SHALL be `std::unordered_map<std::string, CactusTemplateFactory>` where `CactusTemplateFactory` is `entt::entity(*)(entt::registry&)`. Each `pub template` declaration in the module SHALL have an entry mapping its name (as a string literal) to its `create_<snake_case_name>` factory function. Private templates (no `pub` modifier) SHALL NOT be registered.

#### Scenario: Registry contains pub templates only
- **WHEN** a module declares `pub template Box` and `template InternalBase` (no pub)
- **THEN** `cactus_template_registry` contains an entry for `"Box"` and no entry for `"InternalBase"`

#### Scenario: Registry is empty when no pub templates exist
- **WHEN** a module declares no `pub template`
- **THEN** `cactus_template_registry` is an empty map

#### Scenario: Registry entries match factory function names
- **WHEN** a module declares `pub template PlayerSpawn`
- **THEN** `cactus_template_registry["PlayerSpawn"]` points to `create_player_spawn`

### Requirement: editor_spawn_template resolves template name via the registry
`editor_spawn_template(registry, template_name, position_2d, position_3d)` SHALL look up `template_name` in `cactus_template_registry`. If found, it SHALL call the factory function to create a new entity, then patch the entity's `LocalTransform.position` to `position_2d` (if the entity has `LocalTransform`) and trigger a `WorldTransform` sync by also setting `WorldTransform.position` to `position_2d`. If the name is not found, it SHALL return `entt::null`.

#### Scenario: Spawn known template
- **WHEN** `editor_spawn_template(registry, "Box", {5.0, 3.0}, {})` is called and "Box" is registered
- **THEN** a new entity is created with `LocalTransform.position = {5.0, 3.0}` and `WorldTransform.position = {5.0, 3.0}`
- **THEN** the returned entity handle is valid

#### Scenario: Spawn unknown template returns null
- **WHEN** `editor_spawn_template(registry, "Unknown", {0,0}, {})` is called
- **THEN** `entt::null` is returned and no entity is created

#### Scenario: Registry is accessible from EditorTemplatePalette
- **WHEN** `EditorTemplatePalette` iterates `cactus_template_registry` to render buttons
- **THEN** it sees all registered `pub template` names without additional setup
