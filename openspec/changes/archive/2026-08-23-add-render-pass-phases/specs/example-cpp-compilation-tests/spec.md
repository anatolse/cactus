## Purpose

Extend curated example-compilation integration coverage to include the gradient-square example,
mirroring the existing particle-burst curated case.

## ADDED Requirements

### Requirement: Gradient square example is curated for cpp-entt compilation

Automated example-compilation integration coverage SHALL include the gradient square example as a
named curated `cpp-entt` case.

#### Scenario: Curated example list includes gradient square

- **WHEN** `ExampleCppCompilationTests` enumerates curated examples
- **THEN** it includes a case named `gradient-square` with source
  `examples/gradient-square/gradient_square.cactus` and backend `cpp-entt`

#### Scenario: Gradient square generated output is compiled

- **WHEN** the curated example compilation test runs the `gradient-square` case
- **THEN** it invokes the compiler for the example, verifies the generated C++ exists, builds the
  registered example target, and runs the standard formatting and tidy validation steps
