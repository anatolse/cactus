## MODIFIED Requirements

### Requirement: Command-line argument parsing
The CLI SHALL accept the following arguments: input file path (positional, required), `--backend` flag (`cpp-manual` or `cpp-entt`, default: `cpp-entt`), `--output` flag (output file path, optional, defaults to stdout), `--module-path` flag (repeatable, additional directories to search for module files), and `--help` flag.

#### Scenario: Valid invocation with manual backend
- **WHEN** the user runs `cactus game.cactus --backend cpp-manual --output game.cpp`
- **THEN** the CLI compiles `game.cactus` using the manual SoA backend and writes output to `game.cpp`

#### Scenario: Default backend
- **WHEN** the user runs `cactus game.cactus`
- **THEN** the CLI uses the `cpp-entt` backend by default

#### Scenario: Module path flag
- **WHEN** the user runs `cactus main.cactus --module-path ./lib --module-path ./vendor`
- **THEN** the CLI adds `./lib` and `./vendor` to the module search paths

#### Scenario: Invalid backend name
- **WHEN** the user runs `cactus game.cactus --backend rust`
- **THEN** the CLI prints an error "unknown backend 'rust'" and exits with non-zero status
