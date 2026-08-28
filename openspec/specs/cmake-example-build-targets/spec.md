# cmake-example-build-targets Specification

## Purpose

Define how Cactus examples are registered as generated C++ executable targets in the repository CMake build graph.

## Requirements

### Requirement: CMake provides reusable Cactus example target registration
The repository CMake configuration SHALL provide a reusable function for registering a Cactus example as a generated C++ executable target in the regular build graph.

#### Scenario: Function registers a generated executable
- **WHEN** CMake configures an example through the helper function
- **THEN** the build graph contains a generated C++ output produced by invoking the `cactus` compiler on that example source
- **AND** the build graph contains an executable target that compiles and links that generated output

#### Scenario: Generated source is tracked by CMake
- **WHEN** the helper function creates the generated C++ source path
- **THEN** the source is marked as generated and the custom generation step depends on the `cactus` compiler target and the example's root `.cactus` source file

#### Scenario: Unsupported backend is rejected
- **WHEN** a caller requests an unsupported backend in the helper function
- **THEN** CMake configuration fails with a diagnostic that identifies the unsupported backend

#### Scenario: Removed manual backend is rejected
- **WHEN** a caller requests the removed `cpp-manual` backend in the helper function
- **THEN** CMake configuration fails with a diagnostic that identifies `cpp-manual` as unsupported

### Requirement: Buildable example executable targets participate in regular builds
Cactus example executable targets that currently generate and compile cleanly SHALL be regular default build-tree targets rather than test-only or excluded-from-default targets. Existing examples that are not currently buildable SHALL still be exposed as explicit named build-tree targets, but SHALL NOT be part of the default build until their source or backend issues are fixed.

#### Scenario: Example target is visible in the generated build tree
- **WHEN** CMake configures the repository
- **THEN** each registered Cactus example executable target is available by name to the selected build tool

#### Scenario: Normal build compiles registered examples
- **WHEN** a developer runs the regular repository build without selecting a test-only target
- **THEN** currently buildable Cactus example executable targets are part of the default build graph and are eligible to be generated, compiled, and linked

#### Scenario: Buildable example target is not excluded from all
- **WHEN** the helper function declares a currently buildable example executable
- **THEN** it does not declare that executable with `EXCLUDE_FROM_ALL`

#### Scenario: Currently broken example target is explicit opt-in
- **WHEN** the helper function declares an example executable for an existing example that does not currently generate or compile successfully
- **THEN** it declares that executable as an explicit named target that is excluded from the default build graph

### Requirement: Existing example entrypoints are registered
The build configuration SHALL call the reusable example-registration function for every existing standalone Cactus example entrypoint under `examples/` that represents an application or curated compiler example.

#### Scenario: Top-level standalone examples are registered
- **WHEN** CMake configures example targets
- **THEN** it registers targets for `examples/shooter-slice.cactus` and `examples/stdlib_extern_coverage.cactus`

#### Scenario: Directory-based examples are registered
- **WHEN** CMake configures example targets
- **THEN** it registers targets for `examples/blue-square/square.cactus`, `examples/cactus_shop/main.cactus`, `examples/mesh-renderer/main.cactus`, and `examples/platformer/platformer.cactus`

#### Scenario: Support modules are not treated as standalone applications
- **WHEN** an example directory contains additional `.cactus` files that support its root entrypoint
- **THEN** the build configuration does not create separate executable targets for those support modules unless they are explicitly designated as standalone examples

### Requirement: Example targets use backend-appropriate runtime linkage
Registered Cactus example targets SHALL link against the runtime libraries and third-party targets required by their selected backend.

#### Scenario: EnTT example links EnTT runtime dependencies
- **WHEN** an example is registered for the `cpp-entt` backend
- **THEN** its executable target links against the Cactus EnTT runtime path, Raylib, and EnTT

### Requirement: Example target metadata remains stable for tests and developers
Registered example target names and generated output paths SHALL be stable and documented in CMake variables or compile definitions where integration tests need to reference them.

#### Scenario: Integration tests can build a registered example target
- **WHEN** an integration test needs to compile a generated example
- **THEN** it can reference the same target name and generated C++ output path used by the regular build-tree example target

#### Scenario: Developer can build a single example target
- **WHEN** a developer invokes the selected build tool with a registered example target name
- **THEN** that target generates the example C++ output if needed and compiles the example executable

### Requirement: Split-screen forest bombs example target is registered
The CMake build configuration SHALL register the split-screen forest bombs example through the reusable Cactus example target helper with stable generated output and target metadata.

#### Scenario: CMake registers split-screen forest bombs target
- **WHEN** CMake configures generated example targets
- **THEN** it registers `example_split_screen_forest_bombs_generated` for `examples/split-screen-forest-bombs/forest_bombs.cactus` using the `cpp-entt` backend

#### Scenario: Generated output path is stable
- **WHEN** tests need to reference the split-screen forest bombs generated output
- **THEN** CMake exposes a stable generated C++ path for the example under `${CACTUS_GENERATED_EXAMPLES_DIR}`

#### Scenario: Integration tests receive example metadata
- **WHEN** `test_example_cpp_compilation` is compiled
- **THEN** its compile definitions include the split-screen forest bombs source path, generated C++ path, and registered example target name

### Requirement: Editor 3D example target is registered
The CMake build configuration SHALL register the editor 3D example through the reusable Cactus example target helper with stable generated output and target metadata.

#### Scenario: CMake registers editor 3D target
- **WHEN** CMake configures generated example targets
- **THEN** it registers `example_editor_3d_generated` for `examples/editor-3d/main.cactus` using the `cpp-entt` backend

#### Scenario: Generated output path is stable
- **WHEN** tests need to reference the editor 3D generated output
- **THEN** CMake exposes a stable generated C++ path for the example under `${CACTUS_GENERATED_EXAMPLES_DIR}`

### Requirement: Bouncy bubbles example target is registered

The CMake build configuration SHALL register the bouncy-bubbles example through the reusable Cactus example target helper with stable generated output and target metadata.

#### Scenario: CMake registers bouncy bubbles target
- **WHEN** CMake configures generated example targets
- **THEN** it registers `example_bouncy_bubbles_generated` for `examples/bouncy-bubbles/main.cactus` using the `cpp-entt` backend

#### Scenario: Generated output path is stable
- **WHEN** tests need to reference the bouncy bubbles generated output
- **THEN** CMake exposes a stable generated C++ path for the example under `${CACTUS_GENERATED_EXAMPLES_DIR}`

#### Scenario: Bouncy bubbles target participates in the default build
- **WHEN** the example generates and compiles cleanly against the `cpp-entt` backend
- **THEN** its executable target is a regular default build-tree target and is not declared with `EXCLUDE_FROM_ALL`

### Requirement: Particle burst example target is registered

The CMake build configuration SHALL register the particle-burst example through the reusable
Cactus example target helper with stable generated output and target metadata.

#### Scenario: CMake registers particle burst target

- **WHEN** CMake configures generated example targets
- **THEN** it registers `example_particle_burst_generated` for `examples/particle-burst/particle_burst.cactus` using the `cpp-entt` backend

#### Scenario: Generated output path is stable

- **WHEN** tests need to reference the particle burst generated output
- **THEN** CMake exposes a stable generated C++ path for the example under `${CACTUS_GENERATED_EXAMPLES_DIR}`

#### Scenario: Particle burst target participates in the default build

- **WHEN** the example generates and compiles cleanly against the `cpp-entt` backend
- **THEN** its executable target is a regular default build-tree target and is not declared with `EXCLUDE_FROM_ALL`

### Requirement: Gradient square example target is registered

The CMake build configuration SHALL register the gradient-square example through the reusable Cactus example target helper with stable generated output and target metadata.

#### Scenario: CMake registers gradient square target

- **WHEN** CMake configures generated example targets
- **THEN** it registers `example_gradient_square_generated` for `examples/gradient-square/gradient_square.cactus` using the `cpp-entt` backend

#### Scenario: Generated output path is stable

- **WHEN** tests need to reference the gradient square generated output
- **THEN** CMake exposes a stable generated C++ path for the example under `${CACTUS_GENERATED_EXAMPLES_DIR}`

#### Scenario: Gradient square target participates in the default build

- **WHEN** the example generates and compiles cleanly against the `cpp-entt` backend
- **THEN** its executable target is a regular default build-tree target and is not declared with `EXCLUDE_FROM_ALL`

### Requirement: First-person arena example target is registered
The CMake build configuration SHALL register the first-person arena through the reusable Cactus example target helper as a regular cpp-entt target with stable generated output metadata.

#### Scenario: CMake registers first-person arena target
- **WHEN** CMake configures generated example targets
- **THEN** it registers example_first_person_arena_generated for examples/first-person-arena/main.cactus using the cpp-entt backend

#### Scenario: Generated output path is stable
- **WHEN** tests reference the first-person arena generated output
- **THEN** CMake exposes a stable first-person-arena generated C++ path under CACTUS_GENERATED_EXAMPLES_DIR

#### Scenario: Buildable target participates in the regular build
- **WHEN** the example generates and compiles cleanly
- **THEN** example_first_person_arena_generated is not excluded from the default build