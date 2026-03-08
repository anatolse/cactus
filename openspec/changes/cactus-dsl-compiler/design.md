## Context

This is a greenfield compiler project for the Cactus DSL — a kid-friendly (grades 1-5), declarative, data-oriented game language. There is no existing codebase. The compiler must parse `.cactus` source files through a standard pipeline (lex → parse → semantic analysis → code generation) and produce runnable C++ game code. Two C++ backends are in scope now (manual SoA and EnTT-based); a Rust backend is planned for the future. Raylib is the default presentation/rendering API for generated code.

Key constraints: the language uses indentation-based syntax (like Python/YAML), enforces `persist` and `sync` as required field modifiers for serialization and networking, forbids string literals outside `const` blocks, requires pure `func` functions, and replaces imperative loops with functional collection operations (`map`/`filter`/`reduce`).

## Goals / Non-Goals

**Goals:**
- Build a complete compiler frontend (lexer, parser, semantic analyzer) that produces a decorated AST
- Build two C++ code generator backends in dedicated directories (`backends/cpp-manual/`, `backends/cpp-entt/`)
- Reserve `backends/rust/` for future Rust backend
- Use Raylib as the default presentation API in generated code, with architecture allowing alternative libraries (SDL, etc.)
- Enforce all language safety constraints at compile time (const-string rule, func purity, no recursion, no imperative loops)
- Support `persist` and `sync` field modifiers with proper semantic validation
- Produce compilable C++20 output for a 3D cactus shop game example
- Use CMake as build system, Catch2 for testing

**Non-Goals:**
- Rust backend implementation (planned, not in this change)
- Runtime/VM — this is an ahead-of-time compiler only
- IDE/editor integration (LSP, syntax highlighting)
- Package manager or dependency resolution for `.cactus` modules
- GPU code generation (reserved for future)
- Hot-reload or incremental compilation

## Decisions

### 1. Indentation-sensitive lexer with INDENT/DEDENT tokens

**Decision**: The lexer maintains an indent stack and emits explicit INDENT/DEDENT tokens, similar to Python's tokenizer approach.

**Rationale**: This keeps the parser grammar context-free — the parser sees INDENT/DEDENT as block delimiters (like `{`/`}`) without needing to track whitespace. This is the proven approach used by Python, Haskell (layout rule), and CoffeeScript.

**Alternative considered**: Significant-whitespace parsing in the parser itself — rejected because it couples whitespace handling with grammar rules, making the parser much harder to maintain and test.

### 2. Recursive descent parser (hand-written, not generated)

**Decision**: Hand-written recursive descent parser with precedence climbing for expressions.

**Rationale**: The Cactus grammar is relatively simple (no ambiguity, no left-recursion issues). A hand-written parser gives better error messages, is easier to debug, and avoids external tool dependencies (yacc/bison/ANTLR). Precedence climbing handles operator precedence cleanly.

**Alternative considered**: Parser generator (ANTLR, PEG) — rejected because error message quality is critical for a kids' language, and generated parsers make custom error recovery harder.

### 3. Backends in dedicated directories

**Decision**: Each backend lives in its own top-level directory: `backends/cpp-manual/`, `backends/cpp-entt/`, `backends/rust/` (reserved).

**Rationale**: Clean separation of concerns. Each backend can have its own build configuration, dependencies, and test suite. Adding new backends (Rust, WASM, etc.) doesn't touch existing code. The frontend produces a `DecoratedProgram` that serves as the stable interface between frontend and all backends.

**Alternative considered**: All backends in `src/backend_*/` subdirectories — rejected per user preference for top-level `backends/` organization, which also makes it clearer that backends are independent compilation targets.

### 4. Raylib as default presentation API

**Decision**: Generated game code uses Raylib for rendering, input, and audio by default. The backend architecture allows swapping to SDL or other libraries via configuration.

**Rationale**: Raylib is simple, well-documented, cross-platform, and has a C API that maps cleanly to both C++ and Rust. It's ideal for a kids' game language — minimal boilerplate, easy to understand generated code. SDL is more powerful but more verbose.

**Alternative considered**: SDL as default — rejected because Raylib's API is simpler and more aligned with the "easy for kids to understand" philosophy. SDL remains available as an alternative.

### 5. `persist` and `sync` as explicit required modifiers

**Decision**: Fields that need serialization must be marked `persist`; fields that need network replication must be marked `sync`. These are compile-time modifiers validated by the semantic analyzer. Both are required to be explicitly declared — there is no implicit persistence or sync.

**Rationale**: Explicit marking makes data flow visible. Kids (and their teachers) can see exactly which fields are saved and which are networked. The semantic analyzer validates that `persist` and `sync` are only used on `var` fields (not `let`), and backends generate appropriate serialization/replication code based on these markers.

**Alternative considered**: Automatic persistence for all fields — rejected because implicit behavior hides complexity and makes debugging harder, especially for young learners.

### 6. String pool with const-block enforcement

**Decision**: All string literals must be declared in `const` blocks. The compiler interns all strings into a `StringPool` at compile time, and all runtime string references use pool IDs (uint64_t).

**Rationale**: This eliminates accidental string allocation in hot loops (systems), enables compile-time string deduplication, and makes the generated code more cache-friendly. For kids, it teaches the concept of naming constants.

### 7. Decorated AST as the frontend-backend interface

**Decision**: The semantic analyzer produces a `DecoratedProgram` containing the original AST nodes augmented with resolved types, scope IDs, const-string pool IDs, and a dependency graph. This is the sole interface between frontend and backends.

**Rationale**: Clean separation — backends never need to re-resolve types or scopes. Adding a new backend only requires implementing a visitor/walker over the decorated AST. The dependency graph enables backends to optimize system execution order.

## Risks / Trade-offs

**[Risk] Indentation sensitivity is error-prone for kids** → Mitigation: Excellent error messages with visual indicators ("expected 4 spaces of indentation, found 3"). Consider a future formatter/auto-indent tool.

**[Risk] Two backends doubles code generation maintenance** → Mitigation: Shared `DecoratedProgram` interface means backends are independent. Common patterns (struct emission, event handling) can be extracted into shared utilities if duplication becomes excessive.

**[Risk] C++20 requirement limits platform support** → Mitigation: All major compilers (GCC 10+, Clang 10+, MSVC 19.29+) support C++20. The target audience (kids' game development) will use modern toolchains.

**[Risk] Raylib dependency in generated code** → Mitigation: Backend architecture abstracts the presentation API. Switching to SDL requires implementing an alternative emitter, not changing the frontend or language.

**[Risk] `persist`/`sync` modifiers add complexity to the type system** → Mitigation: They are simple boolean flags on `TypeInfo`, validated only on `var` fields. The semantic analyzer rejects invalid combinations (e.g., `persist` on `let` fields) with clear error messages.

**[Trade-off] No imperative loops (map/filter/reduce only)** → This is intentional for GPU safety and functional programming education, but may frustrate kids used to `for` loops. Mitigation: Good documentation and examples showing idiomatic collection operations.
