## Purpose

Define the shared runtime asset infrastructure used by backend-owned stdlib render rules, including stable asset-handle resolution semantics, eager/lazy registration behavior, and test seams for backend/runtime verification.

## Requirements

### Requirement: Shared runtime asset infrastructure defines stable handle resolution semantics
The runtime SHALL provide a shared asset infrastructure layer that defines registration, resolution, validity, and failure semantics for authored asset-handle types used by stdlib render rules, including at minimum texture, mesh, material, and model handles.

#### Scenario: Asset handle resolves through runtime registry
- **WHEN** a backend-owned stdlib render rule needs to consume a `texture_id`, `mesh_id`, `material_id`, or `model_id`
- **THEN** it resolves that handle through a stable runtime asset infrastructure API rather than ad hoc generated-code logic

#### Scenario: Missing asset behavior is defined
- **WHEN** a backend requests a handle that is not present in the runtime asset infrastructure
- **THEN** the runtime returns a defined missing/invalid result or diagnostic path rather than undefined behavior

### Requirement: Asset runtime infrastructure supports both eager and lazy registration flows
The asset runtime infrastructure SHALL support both startup-time eager registration and lazy/on-demand registration or loading, while preserving one consistent handle-resolution contract for backend/runtime consumers.

#### Scenario: Assets are registered eagerly at startup
- **WHEN** a project or backend chooses startup-time registration
- **THEN** the asset runtime infrastructure accepts pre-registration of required asset handles before render/light rules execute

#### Scenario: Assets are registered lazily on demand
- **WHEN** a project or backend chooses lazy registration/loading
- **THEN** the asset runtime infrastructure resolves or materializes assets on demand through the same shared lookup contract used by eager registration

### Requirement: Asset infrastructure supports backend test seams
The asset runtime infrastructure SHALL provide a testable registration/lookup path that allows backend tests to validate stdlib-owned render/light behavior without depending exclusively on opaque production asset-loading flows.

#### Scenario: Backend test registers fake assets
- **WHEN** backend conformance or runtime tests execute against stdlib-owned render rules
- **THEN** they can register or substitute controlled asset records through the shared asset infrastructure

### Requirement: Model assets materialize lazily from their registered path
Model asset registration SHALL store the asset's path without performing file I/O. The runtime SHALL load the model file on first materialization (first render-time resolution of the handle), after the graphics context exists. Load failure (missing file, unreadable file, or a file yielding zero meshes) SHALL mark the record as failed so subsequent resolutions return the defined failure result without retrying the load every frame.

#### Scenario: Registration performs no file I/O
- **WHEN** generated startup code registers a model asset whose file does not exist
- **THEN** registration succeeds and no error occurs until the handle is first materialized

#### Scenario: First materialization loads the file once
- **WHEN** a registered model handle is resolved for rendering across multiple frames
- **THEN** the model file is loaded exactly once and subsequent resolutions reuse the loaded resource

#### Scenario: Failed load is recorded and not retried per frame
- **WHEN** materialization of a model handle fails
- **THEN** the record is marked failed, subsequent resolutions return the failure result, and the file is not re-read on every frame

### Requirement: Model asset resolution does not depend on the runtime process's working directory
A registered model asset's path SHALL resolve successfully at materialization time regardless of whether the running process's current working directory matches the working directory the compiler used when it baked that path. The runtime SHALL discover a usable root directory for resolving registered asset paths and reuse that discovery for the remainder of the process, rather than assuming the working directory in effect at process start is already correct.

#### Scenario: Model resolves when launched from its own directory
- **WHEN** a generated executable containing a registered model asset is launched with its current working directory set to the directory containing the executable itself, rather than the project root the compiler used
- **THEN** the model still materializes successfully on first render-time resolution

#### Scenario: Model resolves when launched from the working directory the path was baked against
- **WHEN** a generated executable is launched with its current working directory already matching the path the compiler baked
- **THEN** the model materializes successfully, unchanged from prior behavior

#### Scenario: Genuinely missing model still fails, once
- **WHEN** a registered model's path does not exist under any directory the runtime is willing to try
- **THEN** materialization fails and is recorded as failed exactly as an unresolvable path already behaves today, without retrying the search on every frame