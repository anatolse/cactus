## Why

The Cactus compiler currently processes a single `.cactus` file at a time. Real games (like the platformer example) split code across multiple modules (`player.cactus`, `enemies.cactus`, `level.cactus`, etc.) connected by `module`/`use` declarations. Although the parser recognizes these declarations, the semantic analyzer ignores them — cross-module type references fail, and the user must manually concatenate files. This makes multi-file projects impossible to compile correctly.

## What Changes

- **Module dependency resolver**: Given a root `.cactus` file, recursively discover all dependencies via `use` declarations, locate `.cactus` files on the filesystem (one module = one file, folders create namespaces via dot notation: `use enemies.walker` → `enemies/walker.cactus`), build a dependency DAG, and topologically sort for compilation order. Same-name files in different folders are distinct modules.
- **Module-aware semantic analysis**: Extend the semantic analyzer with qualified access (`module.Symbol`), module aliases (`use module as m`), unqualified shortcut for unique names, filter clause aliases (`filter: [mod.Trait as t]`), and trait field disambiguation when filtered traits share field names
- **Module artifact serialization**: Each compiled module produces a binary `.cmod` artifact in the `build/` folder containing the DecoratedProgram with public symbols, avoiding keeping all modules in memory simultaneously
- **Program linker**: Merge module artifacts into a single combined `DecoratedProgram` for code generation, detecting cross-module conflicts (duplicate names, missing pub visibility)
- **CLI update**: Accept a root file and automatically resolve all module dependencies; add `--module-path` flag for additional search directories
- **Scope**: Frontend only (parser, semantic analyzer, module resolver, linker). Backend changes deferred to separate change.

## Capabilities

### New Capabilities
- `module-resolver`: Dependency discovery, file location, DAG construction, topological sorting, circular dependency detection
- `module-artifact`: Binary `.cmod` serialization/deserialization of per-module DecoratedProgram and public symbols to/from `build/` folder
- `program-linker`: Cross-module DecoratedProgram merging from artifacts, pub visibility enforcement, conflict detection, combined output generation

### Modified Capabilities
- `dsl-semantic-analysis`: Extend to accept imported symbol tables from dependency modules for cross-module type resolution
- `compiler-cli`: Accept root file with automatic dependency resolution; add `--module-path` search path support

## Impact

- **Frontend**: `src/frontend/semantic_analyzer.h/.cpp` — new `ImportedSymbols` parameter, pub visibility checks
- **New files**: `src/frontend/module_resolver.h/.cpp`, `src/frontend/module_artifact.h/.cpp`, `src/frontend/program_linker.h/.cpp`
- **Build folder**: `build/<module_name>.cmod` binary artifacts for each compiled module
- **CLI**: `src/main.cpp` — multi-file pipeline replacing single-file pipeline
- **Tests**: New test files for module resolver, module artifact serialization, program linker, and multi-module integration
- **No backend changes**: Backends consume the same merged `DecoratedProgram` unchanged
- **No breaking changes**: Single-file compilation still works (treated as a single-module program with no imports)
