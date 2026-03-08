## Why

Kids (grades 1-5) need a simple, safe language for building games — but existing options are either too complex (C++, Rust) or too limited (Scratch). Cactus DSL fills this gap: a declarative, YAML-like language that compiles to high-performance ECS game code. By building a proper compiler (frontend + multiple backends), we enable kids to write readable `.cactus` files that produce production-quality game code — no manual C++/Rust required.

## What Changes

- **New compiler frontend**: Indentation-sensitive lexer, recursive descent parser, and semantic analyzer that produces a decorated AST from `.cactus` source files
- **New C++ Manual SoA backend** (`src/backends/cpp-manual/`): Code generator that emits hand-rolled Structure-of-Arrays C++ code with system loops and event buffers
- **New C++ EnTT backend** (`src/backends/cpp-entt/`): Code generator that emits EnTT ECS library-based C++ code with registry views and dispatchers
- **Planned Rust backend** (`src/backends/rust/`): Future code generator targeting Rust — not in scope for initial implementation but directory structure reserved
- **New CLI tool**: Command-line interface to run the full compile pipeline (lex → parse → analyze → generate) with backend selection
- **New type system**: Static typing with inference, including primitives (`int`, `float`, `bool`, `string`, `vec2`, `vec3`, `quat`, `color`, `entity_id`), composites (`struct`, `enum`, `list[T]`), and field modifiers (`let`, `var`, `persist`, `sync`, `pub`)
- **Language safety constraints**: String literals forbidden outside `const` blocks, pure `func` functions (no `emit`/mutation), no recursion, no imperative loops (only `map`/`filter`/`reduce`)
- **Presentation API**: Raylib as the default rendering/presentation library for generated code; other libraries (SDL, etc.) can be supported via backend configuration
- **Example game**: Cactus shop 3D third-person game written in `.cactus` files as acceptance test
- **Formal language spec**: EBNF grammar, keyword table, type rules, and semantic constraints documented in `spec/cactus_dsl_spec.md`

## Capabilities

### New Capabilities
- `dsl-lexer`: Indentation-sensitive tokenizer that produces INDENT/DEDENT tokens, handles keywords, literals, hex colors, and comments
- `dsl-parser`: Recursive descent parser for all Cactus constructs (module, const, struct, enum, trait, unit, system, view, event, func, interface) producing an undecorated AST
- `dsl-semantic-analysis`: Type resolution, scope resolution, const-string enforcement, func purity checking, no-recursion validation, and dependency graph construction — producing a decorated AST
- `dsl-type-system`: Static type system with inference, covering primitives, composites, field modifiers (`persist`, `sync`, `let`, `var`, `pub`), and special constraints
- `backend-cpp-manual`: Code generator in `src/backends/cpp-manual/` emitting hand-rolled SoA structs, system iteration loops, and event buffers in C++20 with Raylib as default presentation API
- `backend-cpp-entt`: Code generator in `src/backends/cpp-entt/` emitting EnTT component structs, registry views, system functions, and dispatcher setup in C++20 with Raylib as default presentation API
- `compiler-cli`: Command-line interface for running the full pipeline with backend selection (`--backend cpp-manual|cpp-entt`) and output path configuration

### Modified Capabilities

_(none — this is a greenfield project)_

## Impact

- **New codebase**: `src/` for frontend, common utilities, and backends; `src/backends/cpp-manual/` and `src/backends/cpp-entt/` for code generators; `src/backends/rust/` reserved for future Rust backend
- **Build system**: New CMake configuration with FetchContent for Catch2, EnTT, and Raylib dependencies
- **Test suite**: Comprehensive Catch2 tests covering lexer, parser, semantic analysis, and both code generators
- **Dependencies**: C++20 compiler required; Catch2 v3.x for testing; EnTT v3.x for EnTT backend; Raylib for generated game code rendering (SDL as alternative)
- **Example content**: New `examples/cactus_shop/` directory with `.cactus` game files serving as integration test
