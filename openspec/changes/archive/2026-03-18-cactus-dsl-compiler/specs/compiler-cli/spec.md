## ADDED Requirements

### Requirement: Command-line argument parsing
The CLI SHALL accept the following arguments: input file path (positional, required), `--backend` flag (`cpp-manual` or `cpp-entt`, required), and `--output` flag (output file path, optional, defaults to stdout).

#### Scenario: Valid invocation with manual backend
- **WHEN** the user runs `cactus game.cactus --backend cpp-manual --output game.cpp`
- **THEN** the CLI compiles `game.cactus` using the manual SoA backend and writes output to `game.cpp`

#### Scenario: Missing backend flag
- **WHEN** the user runs `cactus game.cactus` without `--backend`
- **THEN** the CLI prints a usage error and exits with non-zero status

#### Scenario: Invalid backend name
- **WHEN** the user runs `cactus game.cactus --backend rust`
- **THEN** the CLI prints an error "unknown backend 'rust'" and exits with non-zero status

### Requirement: Full pipeline execution
The CLI SHALL execute the complete compiler pipeline in order: lexer → parser → semantic analyzer → code generator. If any stage produces errors, the CLI SHALL print all errors and exit without proceeding to the next stage.

#### Scenario: Successful compilation
- **WHEN** the input file is valid `.cactus` source
- **THEN** the CLI runs all stages and outputs generated C++ code

#### Scenario: Parse error stops pipeline
- **WHEN** the input file has a syntax error
- **THEN** the CLI prints the parse error with file/line/column and exits without running semantic analysis or code generation

### Requirement: Error reporting with source locations
The CLI SHALL print all errors and warnings with source file path, line number, and column number in a standard format: `file:line:col: error: message`.

#### Scenario: Error format
- **WHEN** the semantic analyzer detects a string literal in a trait body at line 15, column 8 of `player.cactus`
- **THEN** the CLI prints `player.cactus:15:8: error: string literals are only allowed in const blocks`

### Requirement: Exit codes
The CLI SHALL exit with code 0 on success and code 1 on any compilation error.

#### Scenario: Success exit code
- **WHEN** compilation succeeds
- **THEN** the process exits with code 0

#### Scenario: Error exit code
- **WHEN** compilation fails due to errors
- **THEN** the process exits with code 1
