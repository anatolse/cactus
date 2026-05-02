# example-cpp-compilation-tests Specification

## Purpose
TBD - created by archiving change add-example-cpp-compilation-tests. Update Purpose after archive.
## Requirements
### Requirement: Example compilation integration coverage validates curated examples
The system SHALL provide automated integration coverage that runs the compiler on curated example inputs from `examples/` and then compiles the generated C++ output.

#### Scenario: Blue-square example is validated
- **WHEN** the integration coverage runs for the curated example set
- **THEN** it includes `examples/blue-square` as a required example case

#### Scenario: Mesh renderer example is validated
- **WHEN** the integration coverage runs for the curated example set
- **THEN** it includes `examples/mesh-renderer/main.cactus` as a required cpp-entt example case

#### Scenario: Platformer example is validated
- **WHEN** the integration coverage runs for the curated example set
- **THEN** it includes `examples/platformer/platformer.cactus` as a required cpp-entt example case

#### Scenario: Generated output is compiled after code generation
- **WHEN** an example is selected for integration validation
- **THEN** the coverage first generates C++ from the example's `.cactus` input and then invokes a C++ compilation step on that generated source

### Requirement: Curated cpp-entt examples compile linked with the standard runtime
Curated cpp-entt examples SHALL compile generated project-specific C++ together with the standard Cactus cpp-entt runtime/backend libraries.

#### Scenario: Platformer uses standard runtime-linked pattern
- **WHEN** the platformer curated example target is built
- **THEN** the target links the generated platformer C++ against the standard cpp-entt runtime/backend library

#### Scenario: Missing platformer runtime binding fails integration
- **WHEN** generated platformer C++ references a stdlib-owned input, transform, or render binding that is not provided by the cpp-entt runtime/backend library
- **THEN** the curated example compilation workflow fails and identifies the `platformer` example case

### Requirement: Example compilation failures identify the failing example
The integration coverage SHALL fail with output that identifies which example case did not produce compilable generated C++.

#### Scenario: One example fails compilation
- **WHEN** generated C++ for a curated example fails to compile
- **THEN** the reported failure names that example case and marks the integration check as failed

### Requirement: Generated example output passes repository formatting and tidy checks without fixes
The integration coverage SHALL run `clang-format` and `clang-tidy` from the repository root against generated example C++ output in verification-only mode. The workflow SHALL NOT auto-fix generated files during this validation.

#### Scenario: Generated code is not properly formatted
- **WHEN** generated C++ for a curated example differs from repository `clang-format` expectations
- **THEN** the integration coverage fails without rewriting the generated file

#### Scenario: Generated code triggers clang-tidy diagnostics
- **WHEN** generated C++ for a curated example produces `clang-tidy` diagnostics under the repository configuration
- **THEN** the integration coverage fails and reports the diagnostics without applying fixes

### Requirement: Curated example list is incrementally extensible
The integration coverage SHALL be structured so additional examples under `examples/` can be added without redesigning the harness.

#### Scenario: New example is added later
- **WHEN** maintainers add another supported example case to the curated list
- **THEN** the existing integration harness can validate it using the same generation-and-compilation workflow

### Requirement: compilation coverage includes stdlib extern backend surface cases
Automated compilation/integration coverage SHALL include curated cases that exercise stdlib extern functions and recognized stdlib extern systems across the supported backend paths.

#### Scenario: Extern function coverage example is compiled
- **WHEN** example-compilation integration coverage runs
- **THEN** it includes at least one curated case that imports stdlib math/input extern declarations and compiles generated output that links against the backend runtime library

#### Scenario: Recognized extern system coverage example is compiled
- **WHEN** example-compilation integration coverage runs
- **THEN** it includes at least one curated case that exercises recognized stdlib extern systems such as hierarchy propagation or rendering-backed behavior

#### Scenario: Mesh renderer coverage example is compiled
- **WHEN** example-compilation integration coverage runs
- **THEN** it includes a curated cpp-entt case that imports `std.render.meshes` and exercises the recognized `MeshRenderer` extern system

### Requirement: compilation coverage catches unresolved stdlib backend symbols
The example-compilation integration workflow SHALL fail if generated output for a curated stdlib extern coverage case does not link because a required stdlib backend symbol or binding path is missing.

#### Scenario: Missing stdlib backend symbol fails integration
- **WHEN** a curated example references a stdlib extern function or recognized stdlib extern system whose backend binding is absent
- **THEN** the compilation/integration workflow fails and identifies the affected example case

### Requirement: Mesh renderer example remains a named curated compilation case
Automated example-compilation integration coverage SHALL include the mesh renderer example as a named curated `cpp-entt` case so changes to its authored controls or backend-owned lighting path continue to receive generation-and-compilation coverage.

#### Scenario: Curated example list includes the mesh renderer example
- **WHEN** example-compilation integration coverage runs
- **THEN** it includes `examples/mesh-renderer/main.cactus` as a named curated example case for the `cpp-entt` backend

### Requirement: Curated examples validate backend-generated entrypoint linking
Example-compilation integration coverage SHALL validate that curated generated examples build from generated C++ output containing the selected backend's generated `main()`, linked against the selected backend/runtime library.

#### Scenario: Curated EnTT example uses generated EnTT main
- **WHEN** the integration coverage builds a curated `cpp-entt` generated example executable
- **THEN** the generated C++ output contains the EnTT backend-generated `main()` and links with the standard cpp-entt runtime/backend library

#### Scenario: Curated manual example uses generated manual main
- **WHEN** the integration coverage builds a curated `cpp-manual` generated example executable
- **THEN** the generated C++ output contains the manual backend-generated `main()` and links with the standard cpp-manual runtime/backend library

#### Scenario: Missing backend-generated main fails example compilation
- **WHEN** a curated generated example output lacks the expected backend-generated `main()` and no other valid `main()` is supplied
- **THEN** the compilation/link integration workflow fails and identifies the affected example case

### Requirement: Example coverage rejects CMake-authored entrypoints
Example-compilation integration coverage SHALL verify that standard executable entrypoint behavior comes from generated backend output, not CMake-authored host source files.

#### Scenario: Generated example output defines main
- **WHEN** the integration coverage generates C++ for a curated backend example
- **THEN** the generated output contains a standalone `int main()` implementation emitted by the selected backend

#### Scenario: Build scripts do not create generated host main files
- **WHEN** the integration coverage inspects or configures curated generated-example targets
- **THEN** the target setup does not depend on a CMake-generated host `main.cpp` file for backend startup behavior

### Requirement: Example compilation coverage uses regular example build targets
Example-compilation integration coverage SHALL remain compatible with the regular build-tree example targets created for currently buildable Cactus examples and SHALL NOT require a separate duplicate target-definition mechanism for those curated examples.

#### Scenario: Curated compilation case references regular target metadata
- **WHEN** the integration coverage compiles a curated example that is also registered as a regular example build target
- **THEN** the compilation step uses that regular target's target name and generated C++ output path

#### Scenario: Existing buildable curated coverage remains intact
- **WHEN** example-compilation integration coverage runs after regular example targets are introduced
- **THEN** it still generates, compiles, formats, and runs tidy checks for the curated example cases that currently generate and compile cleanly

#### Scenario: Target setup is not duplicated in tests
- **WHEN** a new Cactus example is added to both regular example targets and curated compilation coverage
- **THEN** maintainers can reuse the regular example target registration metadata instead of hand-writing another generated executable target solely in the test CMake file

