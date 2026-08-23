## Purpose

Extend the existing `cmake-example-build-targets` capability so the gradient-square example
participates in the same reusable Cactus example target registration as `particle-burst` and
every other directory-based example.

## ADDED Requirements

### Requirement: Gradient square example target is registered

The CMake build configuration SHALL register the gradient-square example through the reusable
Cactus example target helper with stable generated output and target metadata.

#### Scenario: CMake registers gradient square target

- **WHEN** CMake configures generated example targets
- **THEN** it registers `example_gradient_square_generated` for
  `examples/gradient-square/gradient_square.cactus` using the `cpp-entt` backend

#### Scenario: Generated output path is stable

- **WHEN** tests need to reference the gradient square generated output
- **THEN** CMake exposes a stable generated C++ path for the example under
  `${CACTUS_GENERATED_EXAMPLES_DIR}`

#### Scenario: Gradient square target participates in the default build

- **WHEN** the example generates and compiles cleanly against the `cpp-entt` backend
- **THEN** its executable target is a regular default build-tree target and is not declared with
  `EXCLUDE_FROM_ALL`
