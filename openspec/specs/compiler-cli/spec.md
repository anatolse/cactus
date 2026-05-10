## Requirements
### Requirement: Command-line argument parsing
The CLI SHALL accept the following arguments: input file path (positional, required), `--backend` flag (`cpp-entt`, default: `cpp-entt`), `--output` flag (output file path, optional, defaults to stdout), `--module-path` flag (repeatable, additional directories to search for module files), and `--help` flag.

#### Scenario: Valid invocation with EnTT backend
- **WHEN** the user runs `cactus game.cactus --backend cpp-entt --output game.cpp`
- **THEN** the CLI compiles `game.cactus` using the EnTT backend and writes output to `game.cpp`

#### Scenario: Default backend
- **WHEN** the user runs `cactus game.cactus`
- **THEN** the CLI uses the `cpp-entt` backend by default

#### Scenario: Module path flag
- **WHEN** the user runs `cactus main.cactus --module-path ./lib --module-path ./vendor`
- **THEN** the CLI adds `./lib` and `./vendor` to the module search paths

#### Scenario: Removed manual backend rejected
- **WHEN** the user runs `cactus game.cactus --backend cpp-manual`
- **THEN** the CLI prints an error identifying `cpp-manual` as an unknown backend and exits with non-zero status

#### Scenario: Invalid backend name
- **WHEN** the user runs `cactus game.cactus --backend rust`
- **THEN** the CLI prints an error "unknown backend 'rust'" and exits with non-zero status

### Requirement: Full pipeline execution
The CLI SHALL execute the complete compiler pipeline: module resolution → per-module (lex → parse → analyze) → link → code generation. For single-file inputs with no `use` declarations, the pipeline SHALL skip module resolution and linking. If any stage produces errors, the CLI SHALL print all errors and exit without proceeding to the next stage.

#### Scenario: Multi-module compilation
- **WHEN** `main.cactus` contains `use player` and `use level`
- **THEN** the CLI resolves dependencies, compiles `player.cactus` and `level.cactus` first, then `main.cactus`, links all modules, and generates a single combined output

#### Scenario: Single-file compilation unchanged
- **WHEN** `standalone.cactus` has no `use` declarations
- **THEN** the CLI compiles it directly without module resolution (backward compatible)

#### Scenario: Module resolution error stops pipeline
- **WHEN** `main.cactus` does `use nonexistent` and the file cannot be found
- **THEN** the CLI prints the error with source location and exits without compiling further

### Requirement: Error reporting with source locations
The CLI SHALL print all errors and warnings with source file path, line number, and column number in a standard format: `file:line:col: error: message`. For multi-module projects, errors SHALL include the originating module's filename.

#### Scenario: Cross-module error
- **WHEN** `enemies.cactus` references undefined type `Foo` at line 10
- **THEN** the CLI prints `enemies.cactus:10:5: error: unknown type 'Foo'`

#### Scenario: Module resolution error
- **WHEN** `main.cactus` has `use missing` at line 3
- **THEN** the CLI prints `main.cactus:3:5: error: module 'missing' not found`

### Requirement: Exit codes
The CLI SHALL exit with code 0 on success and code 1 on any compilation error.

#### Scenario: Success exit code
- **WHEN** compilation succeeds
- **THEN** the process exits with code 0

#### Scenario: Error exit code
- **WHEN** compilation fails due to errors
- **THEN** the process exits with code 1

