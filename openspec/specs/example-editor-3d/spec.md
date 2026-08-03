# example-editor-3d Specification

## Purpose

Define the editor-3d example: scene composition, template palette entries, the 3D placement flow, and edit-mode grid visibility. The example demonstrates the 3D editor path of `std.editor` with animated GLB characters placed on a ground grid.

## Requirements

### Requirement: Editor 3D example composes an editable animated 3D scene
The repository SHALL provide `examples/editor-3d/main.cactus` (module `editor_3d`) importing `std.editor`, `std.input`, `std.transform.volume`, `std.render.models`, `std.render.meshes`, `std.camera.volume`, and `std.camera.viewport`. The scene SHALL declare a fixed angled 3D camera with a viewport and at least one point light, and SHALL set `EditorState.use_3d = true` at startup (e.g. via a load handler, since `std.editor` auto-creates the `Editor` entity). The `EditorTemplatePalette` and `GizmoRenderer3D` extern rules are provided by the `std.editor` import. Model assets SHALL be reused from `examples/model-renderer/art/` (robot and knight GLBs); the example SHALL add no new binary assets.

#### Scenario: Example compiles through the cpp-entt backend
- **WHEN** the `example_editor_3d_generated` target builds
- **THEN** the generated C++ compiles and links without errors

#### Scenario: Editor starts in 3D edit mode
- **WHEN** the example starts
- **THEN** an `EditorState` entity exists with `active = true` and `use_3d = true`

### Requirement: Character templates are palette-spawnable
The example SHALL declare at least two `pub template` character templates (robot and knight), each carrying vec3 `WorldTransform`, `ModelRenderer` bound to its GLB asset, `ModelAnimator` with `playing = true`, and a scale-normalization marker trait. These templates SHALL therefore appear as buttons in the `EditorTemplatePalette` and be spawnable by name via `editor_spawn_template`.

#### Scenario: Palette lists the character templates
- **WHEN** edit mode is active
- **THEN** the template palette renders one button per `pub template`, including the robot and knight templates

#### Scenario: Placing a character on the ground
- **WHEN** the user clicks a character template button (entering Place mode) and then clicks a point in the viewport
- **THEN** a new character entity spawns at the cursor ray's intersection with the y=0 plane
- **AND** the new entity's `ModelAnimator` is playing

### Requirement: Spawned characters are normalized to a target height
The example SHALL normalize each character's scale at spawn time: a rule SHALL detect characters whose scale-normalization flag is unset, compute `TARGET_HEIGHT / models.bounds_size(model).y` (skipping models reporting zero height), write the uniform scale to the transform, and set the flag. Scene-authored and palette-spawned characters SHALL go through the same normalization path.

#### Scenario: Palette-spawned knight matches the target height
- **WHEN** a knight is placed via the palette
- **THEN** within one tick its transform scale is set so the model's bind-pose height equals `TARGET_HEIGHT`

#### Scenario: Unloadable model keeps default scale
- **WHEN** a placed character's model reports zero bounds height
- **THEN** the normalization rule leaves its scale unchanged and does not divide by zero

### Requirement: Ground grid is visible while editing
Because the example declares `GizmoRenderer3D` and its `EditorState` starts active, the y=0 ground grid SHALL be visible on startup, and toggling edit mode with F1 SHALL hide and show the grid together with the palette and edit-mode overlay.

#### Scenario: Grid visible on startup
- **WHEN** the example starts with `EditorState.active = true`
- **THEN** the horizontal grid at y=0 is rendered under the scene

#### Scenario: F1 hides all editor visuals
- **WHEN** the user presses F1 while edit mode is active
- **THEN** the grid, template palette, and edit-mode overlay all disappear, leaving only the scene rendering
