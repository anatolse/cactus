# example-mesh-renderer Specification

## Purpose
TBD - created by archiving change update-mesh-renderer-wasd-point-lights. Update Purpose after archive.
## Requirements
### Requirement: Mesh renderer example exposes authored screen-space WASD rotation controls
The repository SHALL provide `examples/mesh-renderer/main.cactus` as a valid Cactus DSL example that imports the stdlib input and 3D mesh-rendering surfaces and uses authored rules to rotate the demo mesh from WASD input at runtime with screen-space control semantics.

#### Scenario: Example declares WASD-backed input actions
- **WHEN** `examples/mesh-renderer/main.cactus` is read
- **THEN** it imports `std.input` in addition to the mesh-rendering modules
- **AND** it declares input bindings that use the W, A, S, and D keys for mesh rotation control

#### Scenario: Example updates mesh rotation from authored rules
- **WHEN** the example's runtime rules execute after WASD input changes
- **THEN** the demo mesh entity's volume transform rotation is updated by authored Cactus rules rather than project-local C++ callbacks

#### Scenario: WASD maps to screen-space rotation axes
- **WHEN** rotation input is applied repeatedly in the example
- **THEN** A and D produce rotation relative to the screen's vertical axis and W and S produce rotation relative to the screen's horizontal axis
- **AND** the resulting quaternion orientation accumulates so the object's effective rotation axes continue moving in space with the object

### Requirement: Mesh renderer example centers on a simple blue cube
The mesh renderer example SHALL render a simple blue cube as its main subject so the sample stays focused on lighting and rotation behavior instead of complex asset presentation.

#### Scenario: Example configures the demo subject as a blue cube
- **WHEN** `examples/mesh-renderer/main.cactus` is read
- **THEN** the primary rendered mesh subject is authored as a cube
- **AND** its material/presentation is configured to appear blue in the lit scene

### Requirement: Mesh renderer example scene uses two point lights
The mesh renderer example SHALL declare at least two enabled point-light entities together with the demo mesh so the scene exercises multi-light backend rendering behavior.

#### Scenario: Example declares two enabled point-light entities
- **WHEN** `examples/mesh-renderer/main.cactus` is read
- **THEN** it contains at least two units or spawned entities that apply `std.render.meshes.PointLight`
- **AND** each light also provides `std.transform.volume.WorldTransform`
- **AND** each light is enabled in the authored scene configuration

#### Scenario: Lights are arranged to illuminate the demo mesh scene
- **WHEN** the mesh renderer example scene is authored
- **THEN** the point-light transforms are positioned to act as lighting for the demo mesh rather than unrelated unused entities

### Requirement: Mesh renderer example remains compatible with cpp-entt generation
The mesh renderer example SHALL compile with the `cpp-entt` backend while relying on recognized stdlib mesh and point-light bindings.

#### Scenario: Example generates backend-linked project glue
- **WHEN** the compiler is invoked with `cactus examples/mesh-renderer/main.cactus --backend cpp-entt --output <generated.cpp>`
- **THEN** generation succeeds without requiring project-local mesh-renderer or point-light callback implementations

