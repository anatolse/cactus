# example-cpp-compilation-tests Specification

## Purpose
TBD - created by archiving change add-example-cpp-compilation-tests. Update Purpose after archive.
## Requirements
### Requirement: Example compilation integration coverage validates curated examples
The system SHALL provide automated integration coverage that runs the compiler on curated example inputs from `examples/` and then compiles the generated C++ output.

#### Scenario: Blue-square example is validated
- **WHEN** the integration coverage runs for the curated example set
- **THEN** it includes `examples/blue-square` as a required example case

#### Scenario: Generated output is compiled after code generation
- **WHEN** an example is selected for integration validation
- **THEN** the coverage first generates C++ from the example's `.cactus` input and then invokes a C++ compilation step on that generated source

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

