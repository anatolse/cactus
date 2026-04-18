## MODIFIED Requirements

### Requirement: Example compilation integration coverage validates curated examples
The system SHALL provide automated integration coverage that runs the compiler on curated example inputs from `examples/` and then builds the generated C++ project output using the repository’s compiled backend/runtime libraries and any required example-side user library targets.

#### Scenario: Blue-square example is validated
- **WHEN** the integration coverage runs for the curated example set
- **THEN** it includes `examples/blue-square` as a required example case

#### Scenario: Generated output is built through linked project integration
- **WHEN** an example is selected for integration validation
- **THEN** the coverage first generates the example’s project-specific C++ output and then invokes a build step that links it with the appropriate Cactus backend/runtime library and any required example-side user library target

### Requirement: Example compilation failures identify the failing example
The integration coverage SHALL fail with output that identifies which example case did not produce a successfully built generated C++ project.

#### Scenario: One example fails compilation or linkage
- **WHEN** generated C++ project output for a curated example fails to compile or link
- **THEN** the reported failure names that example case and marks the integration check as failed

### Requirement: Generated example output passes repository formatting and tidy checks without fixes
The integration coverage SHALL run `clang-format` and `clang-tidy` from the repository root against generated example C++ project output in verification-only mode. The workflow SHALL NOT auto-fix generated files during this validation.

#### Scenario: Generated code is not properly formatted
- **WHEN** generated C++ for a curated example differs from repository `clang-format` expectations
- **THEN** the integration coverage fails without rewriting the generated file

#### Scenario: Generated code triggers clang-tidy diagnostics
- **WHEN** generated C++ for a curated example produces `clang-tidy` diagnostics under the repository configuration
- **THEN** the integration coverage fails and reports the diagnostics without applying fixes