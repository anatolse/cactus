# backend-cpp-project-linking Specification

## Purpose
TBD - created by archiving change separate-backend-cpp-libraries. Update Purpose after archive.
## Requirements
### Requirement: Generated C++ projects are organized around generated glue plus linked libraries
The C++ compilation model SHALL separate generated project-specific code from reusable backend/runtime code. For each generated C++ project, the final executable or library SHALL be assembled from:

- generated project glue emitted by the compiler,
- a standard Cactus backend/runtime library for the selected backend,
- a user project library when user-defined extern systems or host-side C++ extensions are required.

#### Scenario: EnTT project links generated glue with backend library
- **WHEN** a project is compiled for the `cpp-entt` backend
- **THEN** the resulting build links project-specific generated code against the standard Cactus EnTT backend/runtime library instead of relying on a fully self-contained generated source file

#### Scenario: Manual project links generated glue with backend library
- **WHEN** a project is compiled for the `cpp-manual` backend
- **THEN** the resulting build links project-specific generated code against the standard Cactus manual backend/runtime library instead of relying on a fully self-contained generated source file

### Requirement: Reusable backend support code is not regenerated per project
Reusable backend support code SHALL be compiled into standard Cactus C++ libraries and reused across generated projects. Generated project output SHALL NOT duplicate backend-generic support implementations whose behavior is independent of the authored program.

#### Scenario: Backend runtime helpers come from standard library target
- **WHEN** two different authored projects target the same backend
- **THEN** backend-generic support code is reused from the same standard Cactus backend/runtime library rather than regenerated independently into both project outputs

#### Scenario: Generated output keeps only project-specific glue
- **WHEN** the compiler emits C++ artifacts for a project
- **THEN** the generated output contains only project-specific compiled DSL artifacts, declarations, and registration glue needed to connect to the selected backend/runtime library

### Requirement: External C++ implementations have only two ownership sources
Any external C++ implementation required by a generated project SHALL be provided by exactly one of these ownership sources:

- the standard Cactus C++ backend/runtime libraries, for backend-provided and stdlib-provided behavior,
- the user project library, for project-specific host code and user-defined extern implementations.

Generated output SHALL NOT establish a third ownership path based on ad-hoc regenerated backend support implementations.

#### Scenario: Stdlib-provided extern implementation belongs to Cactus library
- **WHEN** generated code depends on backend-provided stdlib behavior
- **THEN** that implementation is resolved from the standard Cactus backend/runtime library

#### Scenario: Project-specific extern implementation belongs to user library
- **WHEN** generated code depends on a user-defined extern system
- **THEN** that implementation is resolved from the user project library

