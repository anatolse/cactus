## Purpose

Define the behavior of the `cactus` command-line compiler driver, including argument parsing, end-to-end pipeline execution, source-located error reporting, and exit code conventions.

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
The CLI SHALL execute the complete compiler pipeline: module validation/resolution → per-module (lex → parse → analyze) → link when dependencies are present → code generation. Every input file MUST contain an explicit module declaration. For a module with no `use` declarations, the CLI MAY skip dependency traversal and linking, but semantic analysis and code generation still operate with the file's explicit module identity. If any stage produces errors, the CLI SHALL print all errors and exit without proceeding to the next stage.

#### Scenario: Multi-module compilation
- **WHEN** `main.cactus` contains `module main`, `use player`, and `use level`
- **THEN** the CLI resolves dependencies, compiles `player.cactus` and `level.cactus` first, then `main.cactus`, links all modules, and generates a single combined output

#### Scenario: Single-module compilation uses explicit module identity
- **WHEN** `standalone.cactus` declares `module standalone` and has no `use` declarations
- **THEN** the CLI compiles it directly with canonical module identity `standalone`

#### Scenario: Missing module declaration stops pipeline
- **WHEN** `standalone.cactus` has no `module` declaration
- **THEN** the CLI prints the module-declaration error and exits before code generation

#### Scenario: Module resolution error stops pipeline
- **WHEN** `main.cactus` does `use nonexistent` and the file cannot be found
- **THEN** the CLI prints the error with source location and exits without compiling further

### Requirement: Error reporting with source locations
The CLI SHALL print all errors and warnings with source file path, line number, and column number in a standard format: `file:line:col: error: message`. For multi-module projects, errors SHALL include the originating module's filename. Each diagnostic SHALL be printed exactly once.

#### Scenario: Cross-module error
- **WHEN** `enemies.cactus` references undefined type `Foo` at line 10
- **THEN** the CLI prints `enemies.cactus:10:5: error: unknown type 'Foo'`

#### Scenario: Module resolution error
- **WHEN** `main.cactus` has `use missing` at line 3
- **THEN** the CLI prints `main.cactus:3:5: error: module 'missing' not found`

#### Scenario: Each diagnostic is printed once
- **WHEN** compiling a file with one semantic error
- **THEN** the CLI prints that error's line exactly once and exits with code 1

#### Scenario: Warnings on a successful compile are still printed
- **WHEN** compiling a file that succeeds with one warning
- **THEN** the CLI prints that warning exactly once and exits with code 0

#### Scenario: Dependency module parse error is printed once
- **WHEN** compiling a project whose imported module has one parse error
- **THEN** the CLI prints that parse error's line exactly once and exits with code 1

### Requirement: Exit codes
The CLI SHALL exit with code 0 on success and code 1 on any compilation error.

#### Scenario: Success exit code
- **WHEN** compilation succeeds
- **THEN** the process exits with code 0

#### Scenario: Error exit code
- **WHEN** compilation fails due to errors
- **THEN** the process exits with code 1

### Requirement: Warnings can fail the build
The CLI SHALL accept a `--warnings-as-errors` flag. With the flag, when any stage reports a warning, the CLI SHALL print every diagnostic as usual, generate no output, print one summary line with the warning count, and exit with non-zero status. Info diagnostics SHALL NOT count. Without the flag, warnings SHALL NOT change the exit status.

#### Scenario: A warning fails the build
- **WHEN** the user runs `cactus game.cactus --warnings-as-errors --output game.cpp` and compilation reports one warning
- **THEN** the warning is printed once, `game.cpp` is not written, and the exit status is non-zero

#### Scenario: A clean program compiles
- **WHEN** the user runs `cactus game.cactus --warnings-as-errors` and compilation reports no warning
- **THEN** the CLI generates output and exits with zero status

#### Scenario: Warnings pass by default
- **WHEN** the user runs `cactus game.cactus` and compilation reports one warning
- **THEN** the CLI generates output and exits with zero status

#### Scenario: A warning from a dependency counts
- **WHEN** a module the program imports reports a warning and the flag is set
- **THEN** the build fails

### Requirement: Parallel handler option
The CLI SHALL accept a `--parallel-handlers` flag that selects the parallel handler build. Without the flag, generated code SHALL run on one thread. The flag SHALL NOT change which programs compile or any diagnostic.

#### Scenario: Off by default
- **WHEN** the user runs `cactus game.cactus --output game.cpp`
- **THEN** the generated code runs on one thread

#### Scenario: Option selects the parallel build
- **WHEN** the user runs `cactus game.cactus --parallel-handlers --output game.cpp`
- **THEN** the generated code runs eligible handlers on worker threads

#### Scenario: Diagnostics do not change
- **WHEN** one program is compiled with and without the flag
- **THEN** the same diagnostics are reported
