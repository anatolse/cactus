## Context

The Cactus DSL compiler currently operates on a single `.cactus` file. The parser already handles `module` and `use` declarations as AST nodes (`ModuleNode`, `UseNode`), but the semantic analyzer ignores them completely. The platformer example has 7 interconnected files (`main.cactus` uses `player`, `level`, `enemies`, etc.) that cannot be compiled together — cross-module trait references like `Position` (defined in `player.cactus`, used in `enemies.cactus`) are unresolvable.

The existing `DecoratedProgram` struct already aggregates resolved traits, structs, enums, and a dependency graph — it can naturally serve as the compilation cache entry per module. The cpp-manual (SoA) backend already generates a single combined `.cpp` file from a `DecoratedProgram`, so the linker just needs to produce one merged `DecoratedProgram`.

## Goals / Non-Goals

**Goals:**
- Compile multi-module Cactus projects from a single root file with automatic dependency resolution
- Each module compiled exactly once, with its `DecoratedProgram` cached and reused by dependents
- Cross-module `pub` symbol resolution (traits, structs, enums, events, funcs, units)
- Detect circular module dependencies and duplicate symbol definitions with clear error messages
- Merged output remains a single `.cpp` file via the cpp-manual backend
- Backward-compatible: single-file programs (no `module`/`use`) compile identically to before

**Non-Goals:**
- cpp-entt backend support (deferred to separate change)
- Separate per-module `.h`/`.cpp` output (single combined output only)
- Dynamic/runtime module loading
- Selective imports (`use player.Position`) — all `pub` symbols are imported via module reference
- Module versioning or package management

## Decisions

### 1. One module = one file, folders = namespaces

Each `.cactus` file is exactly one module. Folder structure creates namespaces using dot notation:

```
game/
  main.cactus          → module main
  player.cactus        → module player
  enemies/
    walker.cactus      → module enemies.walker
    flyer.cactus       → module enemies.flyer
  lib/
    physics.cactus     → module lib.physics
```

- `use player` resolves to `player.cactus`
- `use enemies.walker` resolves to `enemies/walker.cactus`
- Modules with the same filename in different folders are distinct: `enemies/physics.cactus` and `lib/physics.cactus` are different modules (`enemies.physics` vs `lib.physics`)
- The `module` declaration (if present) is validated to match the folder-qualified name
- Module identity is the full qualified path (e.g., `enemies.walker`), not just the filename

**Alternatives considered:**
- Module registry / manifest file — too heavy for a kids' language
- Slash syntax (`use enemies/walker`) — dots are more natural in a language context

### 2. Qualified access with aliases and auto-shortening

Imported symbols are accessed via their module path: `phys.Body`, `render.Sprite`. The `as` keyword provides short aliases:

```cactus
use phys.body              # access as phys.body.RigidBody
use phys.body as b         # access as b.RigidBody
use render.sprite as s     # access as s.AnimSprite
```

**Unqualified shortcut:** If a pub symbol name is unique across all imported modules (no conflict), it can be used without qualification. If two modules export the same name, the compiler requires qualification and suggests aliases.

```cactus
use player                 # player has pub trait Position (unique)
use enemies                # enemies has pub trait EnemyAI (unique)

system Movement:
  filter: [Position, EnemyAI]   # unqualified — both names are unique
```

**Trait field disambiguation in systems:** When multiple filtered traits have fields with the same name, the field must be qualified by trait name (or alias). No conflict = no qualification needed.

```cactus
system Render:
  filter: [phys.Body as b, render.Sprite as s]
  on update:
    draw(b.x, b.y, s.width, s.height)    # aliases disambiguate

system Simple:
  filter: [Position, Velocity]
  on update:
    position.x += velocity.dx   # no conflict, unqualified access OK
```

**Rationale:** Qualified access prevents subtle bugs from name collisions across modules. Aliases keep code concise. Auto-shortening for unique names keeps simple cases simple — kids don't need to qualify anything until they hit a conflict, at which point the compiler tells them exactly what to do.

**Alternatives considered:**
- Flat import only (all pub symbols dumped into namespace) — fragile, collisions are silent until a new module adds a conflicting name
- Selective imports (`use phys.body.RigidBody`) — too verbose for common case
- Re-exports (`pub use`) — unnecessary complexity at this stage

### 3. File search order

1. Same directory as the importing file
2. Directories listed in `--module-path` (left to right)
3. Root file's directory (always in search path)

**Rationale:** Most projects keep all `.cactus` files in one directory. `--module-path` supports library-style organization later.

### 4. Compilation pipeline: topological sort + disk artifacts

```
parse root → extract `use` deps → recursively parse deps
    → topological sort all modules
    → compile in topo order:
        for each module:
            lex → parse → analyze(with imported symbols from .cmod files)
            → write build/<qualified_name>.cmod to disk
    → link: load all .cmod artifacts → merge → pass to backend
```

Each module is compiled once and its `DecoratedProgram` + public symbols are serialized to a binary `.cmod` file in the `build/` folder. When analyzing module B that does `use A`, we load A's `.cmod` artifact, extract its pub symbols, and inject them as `ImportedSymbols` into B's `SemanticAnalyzer`. This avoids holding all modules in memory simultaneously.

**Alternatives considered:**
- In-memory cache only (`HashMap<string, DecoratedProgram>`) — simpler but doesn't scale, keeps all ASTs in memory
- Lazy compilation (compile on first reference) — topological sort is more predictable and gives better error ordering
- Parallel compilation of independent modules — premature optimization, sequential is fine

### 5. ImportedSymbols as a struct injected into SemanticAnalyzer

```cpp
struct ImportedSymbols {
    std::unordered_map<std::string, ResolvedTrait> traits;
    std::unordered_map<std::string, ResolvedStruct> structs;
    std::unordered_map<std::string, ResolvedEnum> enums;
    std::unordered_set<std::string> event_names;
    std::unordered_set<std::string> func_names;
    std::unordered_set<std::string> unit_names;
};
```

The semantic analyzer's `is_known_type()` and `collect_types()` methods check imported symbols as a fallback after local declarations. No AST merging — imports are resolved at the type/symbol level.

### 6. ProgramLinker merges DecoratedPrograms for codegen

The linker takes all per-module `DecoratedProgram`s and produces one combined `DecoratedProgram` with:
- All traits, structs, enums merged (conflicts = error)
- All system dependency graphs merged
- String pools merged
- A combined `ProgramNode` with all declarations in dependency order

This feeds directly into the existing `CppManualCodegen::generate()` unchanged.

## Risks / Trade-offs

- **[Name collisions across modules]** → Linker detects and reports "duplicate symbol 'X' defined in module A and module B". User must rename. No auto-disambiguation.
- **[Large combined output]** → For projects with many modules, the single `.cpp` file could be large. Acceptable for the target audience; per-module output is a future optimization.
- **[Missing `pub` is confusing]** → A kid forgets `pub` on a trait and gets "unknown type" in another module. Mitigation: error message includes "did you mean to mark it as 'pub'?" when the name exists but isn't exported.
- **[Module declaration mismatch]** → `module foo` in a file named `bar.cactus` produces a clear error: "module name 'foo' does not match filename 'bar'"
