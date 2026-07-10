## Purpose

Define the stdlib `std.render.models` capability: the `ModelRenderer` trait and its backend-owned render system for drawing multi-submesh model assets with their embedded materials, including diagnostic behavior for missing/failed models and test observability.

## Requirements

### Requirement: Model rendering traits are provided by std.render.models
The stdlib SHALL provide a module `std.render.models` containing a passive `pub trait ModelRenderer` with fields `let model: model_id`, `var visible: bool = true`, and `var cast_shadow: bool = true`, and an `extern system ModelRendererSystem` filtered on `std.transform.volume.WorldTransform` and `ModelRenderer`. The trait SHALL NOT contain a material field; materials come from the model file.

#### Scenario: Entity with WorldTransform and ModelRenderer is drawn
- **WHEN** an entity has `std.transform.volume.WorldTransform` and `ModelRenderer` with a valid `model` handle and `visible = true`
- **THEN** the backend render system submits the model for drawing at the entity's world transform without any user-authored system

#### Scenario: Invisible model is skipped
- **WHEN** an entity's `ModelRenderer.visible` is `false`
- **THEN** the model is not submitted for drawing, and the trait remains attached

### Requirement: All submeshes render with their embedded materials
The runtime SHALL draw every submesh of a loaded model using the material bound to that submesh in the model file. Embedded material base color factors SHALL be applied, and loaded materials SHALL be shaded by the same lighting model (point and directional lights) as stdlib mesh rendering.

#### Scenario: Multi-submesh model renders all parts
- **WHEN** a model file containing multiple primitives bound to different embedded materials (e.g., `player.glb` with body/eye/pupil materials) is rendered
- **THEN** every primitive is drawn with its own embedded material's base color

#### Scenario: Loaded models are lit
- **WHEN** a scene contains an active `PointLight` or `DirectionalLight` and a rendered model
- **THEN** the model's submeshes are shaded by the same lighting shader used for stdlib mesh rendering

### Requirement: Missing or failed model assets follow a defined diagnostic path
When a `ModelRenderer` references a model handle whose file is missing, unreadable, or loads with zero meshes, the runtime SHALL skip drawing that entity, SHALL NOT substitute placeholder geometry, and SHALL report a diagnostic at most once per model asset through the runtime's observable debug state.

#### Scenario: Missing model file skips draw with diagnostic
- **WHEN** a registered model path does not exist on disk and an entity submits it for rendering
- **THEN** nothing is drawn for that entity and a diagnostic identifying the asset path is recorded once

#### Scenario: Repeated submissions do not repeat the diagnostic
- **WHEN** the same failed model handle is submitted on subsequent frames
- **THEN** no additional diagnostics are recorded for that handle

### Requirement: Model rendering is observable for tests
The runtime SHALL expose test-observable counters for submitted and drawn models in its render debug state, and model handles SHALL be substitutable through the shared asset registry's test seams without touching the filesystem.

#### Scenario: Backend test observes model submission counts
- **WHEN** a backend conformance test runs a frame with N visible model entities
- **THEN** the render debug state reports N submitted models

#### Scenario: Test registers fake model record
- **WHEN** a test registers a controlled model record through the shared asset registry
- **THEN** resolution succeeds through the same contract as production registration without loading a file
