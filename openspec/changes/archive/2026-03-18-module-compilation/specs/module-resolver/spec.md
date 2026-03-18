## ADDED Requirements

### Requirement: Dependency discovery from use declarations
The module resolver SHALL scan a parsed `ProgramNode` for `UseNode` declarations and return the list of module names that the file depends on.

#### Scenario: File with two use declarations
- **WHEN** `main.cactus` contains `use player` and `use level`
- **THEN** the resolver returns dependency list `["player", "level"]`

#### Scenario: File with no use declarations
- **WHEN** `standalone.cactus` contains no `use` declarations
- **THEN** the resolver returns an empty dependency list

#### Scenario: Use with alias
- **WHEN** `main.cactus` contains `use player as p`
- **THEN** the resolver returns dependency on module `"player"` (alias is ignored for resolution)

### Requirement: File location from module name
The module resolver SHALL locate the `.cactus` file for a given module name by converting dot-separated module paths to filesystem paths (dots → directory separators) and appending `.cactus`. Search directories are checked in order: (1) root file's directory, (2) directories from `--module-path` left to right.

#### Scenario: Simple module found in same directory
- **WHEN** `main.cactus` is at `/game/main.cactus` and `use player` is declared
- **THEN** the resolver finds `/game/player.cactus`

#### Scenario: Dotted module maps to subfolder
- **WHEN** `main.cactus` is at `/game/main.cactus` and `use enemies.walker` is declared
- **THEN** the resolver finds `/game/enemies/walker.cactus`

#### Scenario: Module found via module-path
- **WHEN** `main.cactus` is at `/game/main.cactus`, `player.cactus` is at `/lib/player.cactus`, and `--module-path /lib` is specified
- **THEN** the resolver finds `/lib/player.cactus`

#### Scenario: Same filename in different folders are distinct modules
- **WHEN** `use enemies.physics` and `use lib.physics` are both declared
- **THEN** the resolver finds `enemies/physics.cactus` and `lib/physics.cactus` as two separate modules

#### Scenario: Module not found
- **WHEN** `main.cactus` does `use nonexistent` and no `nonexistent.cactus` exists in any search path
- **THEN** the resolver reports an error "module 'nonexistent' not found" with the source location of the `use` declaration

### Requirement: Dependency DAG construction
The module resolver SHALL build a directed acyclic graph of all module dependencies by recursively resolving transitive dependencies starting from the root file.

#### Scenario: Transitive dependencies
- **WHEN** `main.cactus` uses `enemies`, and `enemies.cactus` uses `player` and `level`
- **THEN** the DAG includes edges: main→enemies, enemies→player, enemies→level

#### Scenario: Diamond dependency
- **WHEN** `main` uses `A` and `B`, both `A` and `B` use `C`
- **THEN** module `C` appears once in the DAG, with edges from both A and B

### Requirement: Topological sort of modules
The module resolver SHALL produce a topological ordering of all modules such that every module is listed after all of its dependencies. This ordering SHALL be used as the compilation order.

#### Scenario: Linear dependency chain
- **WHEN** modules are: main→enemies→player
- **THEN** the compilation order is `[player, enemies, main]`

#### Scenario: Multiple independent dependencies
- **WHEN** main uses player and level (no dependency between player and level)
- **THEN** the compilation order lists player and level before main (their relative order is deterministic but either is valid)

### Requirement: Circular dependency detection
The module resolver SHALL detect circular dependencies and report them as errors with the cycle path.

#### Scenario: Direct circular dependency
- **WHEN** module `A` uses `B` and module `B` uses `A`
- **THEN** the resolver reports an error "circular dependency: A → B → A"

#### Scenario: Indirect circular dependency
- **WHEN** module `A` uses `B`, `B` uses `C`, `C` uses `A`
- **THEN** the resolver reports an error "circular dependency: A → B → C → A"

### Requirement: Module name validation
The module resolver SHALL validate that a file's `module` declaration (if present) matches the filename (without `.cactus` extension). A mismatch SHALL produce an error.

#### Scenario: Matching module name
- **WHEN** file `player.cactus` contains `module player`
- **THEN** the resolver accepts the module declaration

#### Scenario: Mismatched module name
- **WHEN** file `player.cactus` contains `module enemy`
- **THEN** the resolver reports an error "module name 'enemy' does not match filename 'player'"

#### Scenario: Missing module declaration
- **WHEN** file `player.cactus` has no `module` declaration
- **THEN** the resolver infers the module name as `"player"` from the filename

### Requirement: Each module compiled once
The module resolver SHALL ensure each module file is parsed and analyzed at most once, regardless of how many modules depend on it. Results SHALL be cached by canonical file path.

#### Scenario: Shared dependency compiled once
- **WHEN** modules A and B both `use player`
- **THEN** `player.cactus` is parsed and analyzed exactly once; both A and B receive the same cached `DecoratedProgram`
