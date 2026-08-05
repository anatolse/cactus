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
`editor_spawn_template(registry, template_name, position_2d, position_3d)` SHALL look up `template_name` in `cactus_template_registry`. If found, it SHALL call the factory function to create a new entity and patch the entity's transform position; if the name is not found, it SHALL return `entt::null`.

Which position argument is applied SHALL be decided at codegen time from the resolved `WorldTransform.position` field type:
- When `position` is `vec2` (`std.transform.flat`), the generated spawn impl SHALL set `LocalTransform.position` (if present) and `WorldTransform.position` (if present) to `position_2d`.
- When `position` is `vec3` (`std.transform.volume`), the generated spawn impl SHALL set `LocalTransform.position` (if present) and `WorldTransform.position` (if present) to `position_3d`.

The generated spawn impl SHALL be registered whenever the program declares a `WorldTransform` trait; `LocalTransform` patching applies only when that trait also exists in the program.

#### Scenario: Spawn known template in a flat-transform program
- **WHEN** `editor_spawn_template(registry, "Box", {5.0, 3.0}, {})` is called and "Box" is registered in a program using `std.transform.flat`
- **THEN** a new entity is created with `LocalTransform.position = {5.0, 3.0}` and `WorldTransform.position = {5.0, 3.0}`
- **THEN** the returned entity handle is valid

#### Scenario: Spawn known template in a volume-transform program
- **WHEN** `editor_spawn_template(registry, "Robot", {}, {2.0, 0.0, -3.0})` is called and "Robot" is registered in a program using `std.transform.volume`
- **THEN** a new entity is created with `WorldTransform.position = {2.0, 0.0, -3.0}`
- **THEN** the returned entity handle is valid

#### Scenario: Volume-transform template without LocalTransform still spawns
- **WHEN** a registered template has `WorldTransform` (vec3) but no `LocalTransform`
- **THEN** `editor_spawn_template` creates the entity and sets only `WorldTransform.position` to `position_3d`

#### Scenario: Spawn unknown template returns null
- **WHEN** `editor_spawn_template(registry, "Unknown", {0,0}, {})` is called
- **THEN** `entt::null` is returned and no entity is created

#### Scenario: Registry is accessible from EditorTemplatePalette
- **WHEN** `EditorTemplatePalette` iterates `cactus_template_registry` to render buttons
- **THEN** it sees all registered `pub template` names without additional setup

### Requirement: editor_template_names exposes registered template names as a deterministic list
The backend SHALL expose an `editor_template_names()` runtime function returning a `list[string]` of every name currently registered in `cactus_template_registry`. The returned order SHALL be deterministic and match the declaration order of the corresponding `pub template` statements in source — the backend SHALL maintain this ordering explicitly (e.g. via a companion ordered list populated at registration time) rather than relying on the iteration order of the underlying map storage.

The `std.editor` module SHALL declare `pub extern func template_names() list[string]` bridging to this runtime function, for use by DSL-authored palette rendering.

#### Scenario: Template names returned in declaration order
- **WHEN** a module declares `pub template Box` followed by `pub template PlayerSpawn`
- **THEN** `editor_template_names()` returns `["Box", "PlayerSpawn"]`, in that order

#### Scenario: Order is stable across calls within a run
- **WHEN** `editor_template_names()` is called multiple times within the same program run without any template registration changing
- **THEN** every call returns the same order

#### Scenario: Empty registry returns an empty list
- **WHEN** a module declares no `pub template`
- **THEN** `editor_template_names()` returns an empty list

#### Scenario: template_names accessible from a DSL rule
- **WHEN** a rule in `std.editor` calls `template_names()` inside a bounded `for` loop
- **THEN** the loop iterates once per registered `pub template`, bound to that template's name

### Requirement: editor_template_index exposes a name's position in the deterministic order
The backend SHALL expose an `editor_template_index(name)` runtime function returning the zero-based position of `name` within the same declaration-ordered sequence `editor_template_names()` returns, or `-1` if `name` is not a registered template. The `std.editor` module SHALL declare `pub extern func template_index(name: string) int` bridging to this runtime function.

This exists because bounded `for` loops cannot mutate an outer-scope counter across iterations (confirmed unsupported — see `editor-declarative-rendering`'s design notes), so DSL rules that need a per-item index for index-based layout/tint (e.g. `EditorTemplatePalette`) obtain it via this narrow per-name lookup instead of a loop-local accumulator.

#### Scenario: Index matches declaration order
- **WHEN** a module declares `pub template Box` followed by `pub template PlayerSpawn`
- **THEN** `editor_template_index("Box")` returns `0` and `editor_template_index("PlayerSpawn")` returns `1`

#### Scenario: Unknown name returns -1
- **WHEN** `name` does not match any registered `pub template`
- **THEN** `editor_template_index(name)` returns `-1`

#### Scenario: template_index accessible from a DSL rule iterating template_names
- **WHEN** a rule calls `template_index(name)` for each `name` yielded by iterating `template_names()`
- **THEN** the returned indices are `0, 1, 2, ...` in the same order as the iteration
