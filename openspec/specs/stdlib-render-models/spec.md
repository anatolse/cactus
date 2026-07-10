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

### Requirement: std.render.models provides a ModelAnimator trait
The `std.render.models` module SHALL provide a `pub trait ModelAnimator` with fields `var clip: int = 0`, `var playing: bool = true`, `var speed: float = 1.0`, and `var time: float = 0.0`. The trait is optional: entities with `ModelRenderer` but no `ModelAnimator` SHALL render at the model's bind pose, unchanged from pre-animation behavior.

#### Scenario: ModelAnimator fields accessible after import
- **WHEN** authored code contains `use std.render.models` and a unit body applies `ModelAnimator`
- **THEN** the entity has writable fields `clip`, `playing`, `speed`, and `time` with the specified types and defaults

#### Scenario: ModelRenderer without ModelAnimator renders bind pose
- **WHEN** an entity has `ModelRenderer` for a skinned model but no `ModelAnimator`
- **THEN** the model is drawn at its bind pose

### Requirement: ModelAnimationSystem advances animation time
The `std.render.models` module SHALL declare `extern system ModelAnimationSystem` filtered on `ModelRenderer` and `ModelAnimator`, recognized by the backend as an update-phase system. Each update tick, for entities with `playing = true`, the backend SHALL advance `time` by `dt * speed` and wrap it into `[0, duration)` of the active clip, where `duration = keyframeCount / GLTF sampling rate` of that clip. When `playing = false`, `time` SHALL not be modified, freezing the pose. Authored writes to `clip`, `speed`, and `time` SHALL take effect the same frame.

#### Scenario: Playing animator advances and loops
- **WHEN** an entity's `ModelAnimator` has `playing = true` and `time` reaches the end of the active clip
- **THEN** `time` wraps around and playback continues from the start of the clip

#### Scenario: Paused animator freezes pose
- **WHEN** an entity's `ModelAnimator.playing` is set to `false`
- **THEN** `time` stops advancing and the rendered pose remains at the frame corresponding to the frozen `time`

#### Scenario: Negative or scaled speed respected
- **WHEN** `ModelAnimator.speed` is `2.0`
- **THEN** `time` advances at twice real time

### Requirement: Animated models are rendered with GPU skinning
For entities with `ModelRenderer` and `ModelAnimator` whose model has a skeleton (`boneCount > 0`), the backend SHALL compute bone matrices for the animator's `(clip, time)` and draw all submeshes with a GPU skinning shader that transforms both vertex positions and normals by the bone matrices, preserving the existing lighting model. Skinning SHALL NOT mutate or re-upload vertex buffers (no CPU skinning path). Models without a skeleton SHALL continue to render through the existing non-skinned shader path.

#### Scenario: Skinned model deforms per its clip
- **WHEN** a skinned model entity has `ModelAnimator` with a valid `clip` and advancing `time`
- **THEN** the rendered mesh deforms according to that clip's pose at `time`, and lighting responds to the deformed normals

#### Scenario: Static models unaffected by skinning path
- **WHEN** a model with `boneCount = 0` (e.g. a static prop) is rendered in a program that also renders skinned models
- **THEN** the static model renders exactly as before through the non-skinned shader

### Requirement: Entities sharing a model asset animate independently
Multiple entities referencing the same `model_id` SHALL each render with their own `ModelAnimator` pose in the same frame. Per-entity pose state SHALL be limited to the entity's `ModelAnimator` fields; the backend re-poses shared model resources per draw submission.

#### Scenario: Three entities, three different clips
- **WHEN** three entities share one skinned model asset and their `ModelAnimator.clip` values differ
- **THEN** each entity is drawn with its own clip's pose in the same rendered frame

#### Scenario: Same clip, different times
- **WHEN** two entities share a model and clip but have different `time` values
- **THEN** each is drawn at its own point in the clip

### Requirement: Animation clip introspection functions
The `std.render.models` module SHALL provide `pub extern func animation_count(m: model_id) int` returning the number of animation clips in the model, and `pub extern func animation_name(m: model_id, clip: int) string` returning the clip's name as stored in the model file. For an unresolvable model handle, `animation_count` SHALL return `0`; for an out-of-range `clip` index or unresolvable handle, `animation_name` SHALL return `""`. Both functions SHALL work before the model's first draw (introspection triggers lazy load).

#### Scenario: Clip count for a loaded model
- **WHEN** `models.animation_count(Robot)` is called for a GLB with 14 clips
- **THEN** it returns `14`

#### Scenario: Clip name lookup
- **WHEN** `models.animation_name(Robot, 2)` is called and the model's clip 2 is named `Robot_Idle`
- **THEN** it returns `"Robot_Idle"`

#### Scenario: Out-of-range clip name is empty
- **WHEN** `models.animation_name(Robot, 99)` is called on a model with 14 clips
- **THEN** it returns `""`

#### Scenario: Introspection before first render
- **WHEN** `animation_count` is called in a system tick before the model has ever been drawn
- **THEN** the model loads lazily and the correct count is returned

### Requirement: Invalid animation state degrades to bind pose with diagnostic
When a `ModelAnimator.clip` is negative, `>=` the model's clip count, or the model has no animations, the backend SHALL render the entity at bind pose, SHALL NOT crash or substitute other clips, and SHALL record a diagnostic in the runtime's observable render debug state at most once per (model asset, clip index) pair. Models whose skeleton exceeds the skinning bone limit SHALL load with a diagnostic and render unskinned at bind pose.

#### Scenario: Out-of-range clip renders bind pose once-diagnosed
- **WHEN** an entity's `ModelAnimator.clip = 99` on a model with 14 clips is submitted over many frames
- **THEN** the entity renders at bind pose and exactly one diagnostic for that (asset, clip) is recorded

#### Scenario: Animation-less model with animator
- **WHEN** an entity has `ModelAnimator` but its model contains zero animation clips
- **THEN** the entity renders at bind pose with a single recorded diagnostic
