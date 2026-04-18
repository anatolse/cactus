## 1. Build and runtime target restructuring

- [x] 1.1 Define the repository CMake target layout for shared runtime support and backend-specific C++ runtime libraries.
- [x] 1.2 Move reusable backend/runtime implementation code out of per-project codegen paths and into the new standard Cactus library targets.
- [x] 1.3 Ensure backend-specific generated outputs can include and link against the new library-facing headers/APIs.

## 2. Backend code generation refactor

- [x] 2.1 Refactor the `cpp-entt` backend to emit project-specific glue instead of a fully self-contained generated implementation by default.
- [x] 2.2 Refactor the `cpp-manual` backend to emit project-specific glue instead of a fully self-contained generated implementation by default.
- [x] 2.3 Replace inline generation of stdlib-recognized extern system implementations with bindings to standard Cactus backend library implementations.
- [x] 2.4 Preserve generated declarations/registration metadata needed to connect authored DSL systems to the backend runtime libraries.

## 3. Extern system ownership model

- [x] 3.1 Update user-defined `extern system` code generation so callback declarations remain generated but implementations are expected from a user library target.
- [x] 3.2 Ensure generated scheduling and iteration glue still invokes user-defined extern callbacks with the correct typed signatures and ordering behavior.
- [x] 3.3 Add or update tests covering link-contract behavior for backend-provided vs user-provided extern system implementations.

## 4. Example and integration workflow updates

- [x] 4.1 Update curated example build configuration to link generated project output against the selected Cactus backend/runtime library.
- [x] 4.2 Add example-side user library wiring where custom host C++ code is required.
- [x] 4.3 Update example compilation integration tests to validate compile-and-link behavior for the new linked-project model.

## 5. Verification

- [x] 5.1 Run backend and integration tests covering both `cpp-entt` and `cpp-manual` generated project linking.
- [x] 5.2 Verify generated examples still pass formatting and clang-tidy checks under the new project-glue output model.
- [x] 5.3 Review the final implementation against the proposal, design, and spec deltas for consistency.