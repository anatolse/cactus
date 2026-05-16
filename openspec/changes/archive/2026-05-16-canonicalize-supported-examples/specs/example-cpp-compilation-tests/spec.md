## MODIFIED Requirements

### Requirement: Example compilation integration coverage validates curated examples

The system SHALL provide automated integration coverage that runs the compiler on curated example inputs from `examples/` and then compiles the generated C++ output. Curated examples SHALL remain aligned with the current parser, semantic analyzer, and implemented backend behavior.

#### Scenario: Existing curated example paths remain stable
- **WHEN** an existing curated example remains part of compilation coverage
- **THEN** the coverage continues to reference its existing path unless a separate change intentionally updates that path

#### Scenario: Curated example drift fails validation
- **WHEN** a curated example uses syntax or semantics rejected by the current parser or semantic analyzer
- **THEN** the integration coverage fails and identifies the drifting example case
