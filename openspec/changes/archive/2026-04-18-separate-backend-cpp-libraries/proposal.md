## Why

The current C++ backends emit large self-contained generated files that repeatedly regenerate backend runtime behavior, stdlib integration code, and project bootstrapping. This makes backend support code hard to reuse, prevents stable linkage boundaries for host applications, and leaves no clean contract for integrating user-authored C++ code into generated projects.

We need a build architecture where reusable backend/runtime code lives in stable C++ libraries, while generated output is limited to project-specific glue. This also clarifies ownership of external C++ systems: backend-provided behavior belongs in the standard Cactus C++ libraries, and project-specific behavior belongs in the user’s library.

## What Changes

- Reorganize C++ backend output around reusable backend/runtime libraries instead of regenerating backend support code into every generated source file.
- Define a stable split between generated project glue, standard Cactus backend libraries, and user-provided project libraries.
- Change user-defined `extern system` integration so generated code expects implementations from a user library target instead of embedding or regenerating those implementations.
- Modify backend specs so stdlib-recognized extern systems resolve to standard Cactus backend libraries rather than ad-hoc emitted bodies.
- **BREAKING**: generated backend output will no longer be treated as a fully standalone all-in-one implementation by default; projects must link against the appropriate Cactus backend library and any required user library.

## Capabilities

### New Capabilities
- `backend-cpp-project-linking`: Defines the contract between generated C++ artifacts, standard Cactus backend libraries, and user-provided C++ libraries for compiled projects.

### Modified Capabilities
- `backend-cpp-entt`: Change generated-output requirements so reusable backend behavior is provided by standard Cactus libraries and user-defined extern systems resolve through user libraries.
- `backend-cpp-manual`: Change generated-output requirements so reusable backend behavior is provided by standard Cactus libraries and user-defined extern systems resolve through user libraries.
- `dsl-extern-system`: Clarify that extern implementations must be satisfied either by standard Cactus backend libraries or by a user project library, not regenerated inline each build.
- `example-cpp-compilation-tests`: Update integration expectations to validate linking against backend/user libraries rather than only compiling monolithic generated source.

## Impact

- Affects backend code generators under `src/backends/cpp-entt/` and `src/backends/cpp-manual/`.
- Affects root and example CMake build structure, target layout, and generated-project integration flow.
- Affects example compilation integration tests and any generated example projects.
- Introduces new expectations for host applications and user-authored C++ extension code.