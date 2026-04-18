## Context

Today both C++ backends generate large, mostly self-contained C++ translation units. That generated output mixes three different concerns:

- project-specific compiled DSL content such as traits, units, and authored systems,
- reusable backend/runtime behavior such as scheduling, scene/data loading, rendering integration, and stdlib-recognized extern systems,
- integration seams for user-authored C++ such as custom `extern system` implementations.

This architecture causes backend code to be regenerated into every project, makes example and host-project builds depend on monolithic generated files, and leaves no clear ownership model for external C++ code. The change introduces explicit linkage boundaries so reusable backend behavior ships as stable Cactus libraries, while project-specific and user-specific code remain separate.

## Goals / Non-Goals

**Goals:**
- Define a stable compiled-project architecture for C++ backends.
- Move reusable backend/runtime behavior into standard Cactus C++ libraries per backend.
- Reduce generated output to project-specific declarations, compiled DSL logic, and registration glue.
- Define a clean contract for user-defined `extern system` implementations through a user library target.
- Update verification expectations so generated example projects validate link-time integration, not only monolithic generated source compilation.

**Non-Goals:**
- Replacing the DSL surface for systems, traits, or modules.
- Introducing a new non-C++ backend plugin system in this change.
- Fully redesigning every example project; examples only need enough restructuring to exercise the new contract.
- Prescribing one exact public C API for runtime entry points if an internal C++ API is sufficient.

## Decisions

### Decision: Generated output is split into project glue, standard backend libraries, and a user library

The compiled-project model is:

1. **Generated project glue**: emitted by the compiler and containing only project-specific compiled artifacts such as generated trait/component declarations, generated DSL-authored system bodies, generated metadata/registration tables, and declarations for required extern integrations.
2. **Standard Cactus backend libraries**: built from the repository and reused across projects. These libraries provide reusable runtime code, stdlib-recognized extern system implementations, backend scheduling helpers, and other non-project-specific support.
3. **User library**: authored by the host project and linked into the final executable. This library satisfies user-defined extern system implementations and any host-side extension code.

This gives a strict ownership rule: external C++ behavior must come either from the standard Cactus backend/runtime libraries or from the user library, never from ad-hoc regenerated copies of backend support code.

**Alternatives considered:**
- Keep generating all backend support code inline: simplest short-term, but directly conflicts with the requirement for reusable libraries.
- Put both stdlib and user extern implementations into one generated file: easier to compile, but it erases ownership boundaries and keeps rebuild costs high.

### Decision: Each backend gets a reusable runtime/library target

The repository will expose reusable backend targets with a common/shared layer plus backend-specific layers. Exact target names may evolve, but the architecture is:

- common runtime/support target(s) shared by multiple backends,
- `cpp-entt` runtime/backend library target,
- `cpp-manual` runtime/backend library target.

Backend-specific generated code links only the backend it targets. Shared support may include data-file loading, common runtime declarations, asset lookup helpers, or backend-agnostic utility code.

**Alternatives considered:**
- One giant runtime library for all backends: simpler target graph, but couples unrelated backend dependencies and makes host integration heavier.
- No shared layer at all: avoids abstraction work, but duplicates common support code across backend libraries.

### Decision: Stdlib-recognized extern systems resolve to standard Cactus libraries, not inline generated bodies

Known stdlib extern systems remain backend-provided, but their implementation ownership moves out of generated files. Generated code should call or register backend-library implementations rather than emitting their full bodies into each project.

This preserves the existing semantic model that stdlib extern systems are automatic/backend-provided while changing the deployment model to reusable linkage.

**Alternatives considered:**
- Continue emitting optimized stdlib extern system bodies inline: preserves current codegen shape but violates the reuse requirement.
- Force users to implement stdlib extern systems: contradicts current stdlib behavior and would be a major semantic regression.

### Decision: User-defined extern systems become link contracts against the user library

For non-stdlib `extern system` declarations, generated output should expose typed declarations and the generated scheduling/iteration glue needed to invoke them, but the implementation itself must come from the user library.

The practical contract is:
- generated code declares the user callback symbol(s),
- generated glue invokes those symbol(s),
- the final link succeeds only when the user library provides the required definitions.

This keeps backend-generated query/iteration logic under compiler control while making host-owned behavior explicit and reusable.

**Alternatives considered:**
- Generate editable implementation stubs into the build tree: convenient for prototypes, but fragile for long-lived projects and hostile to source ownership.
- Require users to hand-write full backend iteration loops: too much backend leakage into host code.

### Decision: Standalone generated `main()` becomes optional rather than the default integration contract

Current backends emit a full `main()` and default game loop. Under the new architecture, the default compiled-project contract should prefer a reusable backend runtime entry path so project builds can compose generated glue, backend runtime libraries, and user libraries cleanly.

Examples or special demo modes may still support an all-in-one generated executable path, but that is no longer the normative integration model for compiled projects.

**Alternatives considered:**
- Keep mandatory generated `main()`: simplest for demos, but it tightly couples project bootstrap to generated code and makes user/host integration awkward.
- Remove any runtime loop support entirely: too disruptive and pushes too much boilerplate onto examples and users.

## Risks / Trade-offs

- **Build graph becomes more complex** → Mitigation: define a small, explicit target model and update example/test harnesses to exercise it end-to-end.
- **Breaking existing example integration** → Mitigation: keep an adapter path for examples while moving primary specs to the new linked-project model.
- **Boundary between generated glue and backend libraries may be fuzzy** → Mitigation: define ownership by reuse: anything backend-generic or stdlib-provided belongs in Cactus libraries; anything project-specific stays generated.
- **User-defined extern systems may fail at link time more often during migration** → Mitigation: preserve explicit generated declarations and improve tests/documentation around required user library symbols.

## Migration Plan

1. Introduce backend/runtime library targets in the repository build.
2. Refactor backend codegen to stop emitting reusable backend support inline and instead emit project glue plus library-facing declarations/calls.
3. Move stdlib-recognized extern system implementations behind backend-library entry points.
4. Update user-defined extern system generation to expect definitions from a user library target.
5. Update example projects and integration tests to compile generated glue and link against backend/user libraries.
6. Keep any standalone/demo generation path only as an explicitly secondary mode if still needed.

## Open Questions

- Should the compiler emit one generated source file, generated header/source pairs, or a small generated project directory for the new linkage model?
- What is the minimal stable ABI/API surface between generated glue and backend runtime libraries?
- Do examples keep a convenience fallback path with generated `main()`, or should they all migrate fully to host-owned entry points?