# backend-cpp-project-linking Specification

## Purpose
TBD - created by archiving change separate-backend-cpp-libraries. Update Purpose after archive.
## Requirements
### Requirement: Generated C++ projects are organized around generated glue plus linked libraries
The C++ compilation model SHALL separate generated project-specific code from reusable backend/runtime code. For each generated C++ project, the final executable or library SHALL be assembled from:

- generated project glue emitted by the compiler,
- a standard Cactus backend/runtime library for the selected supported backend,
- a user project library when user-defined extern systems or host-side C++ extensions are required.

#### Scenario: EnTT project links generated glue with backend library
- **WHEN** a project is compiled for the `cpp-entt` backend
- **THEN** the resulting build links project-specific generated code against the standard Cactus EnTT backend/runtime library instead of relying on a fully self-contained generated source file

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

### Requirement: Standard C++ executables use generated output as the entrypoint owner
When a generated C++ project is built as a standard executable, the final target SHALL be assembled from generated project output that contains the selected supported backend's generated `main()`, the selected standard Cactus backend/runtime library, and any required user project library. The build SHALL NOT require a separate host `main.cpp` source for the standard executable path.

#### Scenario: EnTT executable composition uses generated EnTT main
- **WHEN** a generated project targets `cpp-entt` and is built as a standard executable
- **THEN** the final executable target obtains `main()` from the generated cpp-entt output and links the standard Cactus EnTT backend/runtime library

#### Scenario: Runtime libraries remain entrypoint-free
- **WHEN** backend runtime libraries are linked into tests or generated executable targets
- **THEN** those libraries do not themselves provide a `main()` symbol

### Requirement: Build scripts compile but do not author entrypoints
Build configuration SHALL compile generated backend output containing the selected supported backend's emitted entrypoint, but SHALL NOT author entrypoint implementation code as generated build-system text.

#### Scenario: CMake target compiles generated output containing main
- **WHEN** CMake defines a generated-example executable target
- **THEN** the target uses the compiler-produced generated C++ output as the source of `main()`

#### Scenario: CMake target does not generate host main text
- **WHEN** CMake configures generated-example executable targets
- **THEN** the configuration does not write a host `main()` implementation into the build tree as a substitute for backend-owned source

### Requirement: User-library ABI is per external handler
Generated project output SHALL declare one canonical user-library callback ABI per user-defined external handler. The ABI SHALL encode trigger data, selected entity context when applicable, const reads, mutable writes, and capability-limited event, command, and effect interfaces from the handler contract.

#### Scenario: Multiple external handlers link independently
- **WHEN** one extern system handles fixed_tick and Reset
- **THEN** generated output references two distinct user-library symbols and either may produce an independent link error

#### Scenario: Contract change changes ABI
- **WHEN** a trait moves from reads to writes in an external handler contract
- **THEN** the generated callback declaration changes from immutable to mutable trait access

