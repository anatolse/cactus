## 0. Language Spec Update

- [x] 0.1 Update `spec/cactus_dsl_spec.md` — add module system section: `module` declaration with dotted names, `use` with dotted paths and `as` aliases, `pub` visibility modifier on top-level declarations, qualified type references (`module.Type`), filter clause `as` aliases (`filter: [mod.Trait as t]`), trait field disambiguation rules (qualify when ambiguous, unqualified when unique), folder-to-namespace mapping

## 1. Parser Extension

- [x] 1.1 Extend `parse_module()` and `parse_use()` to accept dotted identifiers — `module enemies.walker` and `use enemies.walker` parse as dot-separated module path string
- [x] 1.2 Support `as` alias in `use` declarations — `use phys.body as b` stores `module_name = "phys.body"`, `alias = "b"` in `UseNode`
- [x] 1.3 Support qualified type references — `player.Position`, `b.RigidBody` parse as a `QualifiedName` AST node (prefix + name)
- [x] 1.4 Support `as` aliases in system `filter:` clauses — `filter: [phys.Body as b, render.Sprite as s]` stores each filter entry with optional alias; aliases are local to the system body
- [x] 1.5 Add parser tests — dotted modules, use-as aliases, qualified type references, filter-as aliases, mixed qualified/unqualified in filter lists

## 2. Module Resolver

- [x] 2.1 Define `src/frontend/module_resolver.h` — `ModuleResolver` class with `ModuleInfo` (qualified name, path, ProgramNode, DecoratedProgram), `resolve(root_path, search_paths)` returning compilation order
- [x] 2.2 Implement dependency discovery — scan `ProgramNode` for `UseNode` declarations, extract module names (ignore aliases for file lookup)
- [x] 2.3 Implement file locator — convert dotted module names to filesystem paths (dots → directory separators), search in root dir then --module-path dirs, report error with source location when not found
- [x] 2.4 Implement DAG construction — recursively resolve transitive dependencies, handle diamond deps (deduplicate by canonical path)
- [x] 2.5 Implement topological sort — Kahn's algorithm, producing compilation order (leaves first)
- [x] 2.6 Implement circular dependency detection — report error with full cycle path
- [x] 2.7 Implement module name validation — verify `module X.Y` matches folder/filename, infer qualified name when no declaration present
- [x] 2.8 Write `tests/test_module_resolver.cpp` — dependency extraction, dotted-name file location, DAG with diamond deps, topological ordering, circular dep detection, module name mismatch, same-name files in different folders

## 3. Module Artifact Serialization

- [x] 3.1 Define `src/frontend/module_artifact.h` — `ModuleArtifact` with `save()`, `load()`, and `extract_pub_symbols()` (returns `ImportedSymbols` keyed by symbol name with module source info)
- [x] 3.2 Implement binary format — magic number (`CMOD`), version byte, serialized DecoratedProgram with pub/non-pub visibility flags
- [x] 3.3 Implement `save()` — write to `build/<qualified_name>.cmod`, create `build/` if needed
- [x] 3.4 Implement `load()` — deserialize with version validation
- [x] 3.5 Implement `extract_pub_symbols()` — extract pub-only symbols into `ImportedSymbols` struct
- [x] 3.6 Write `tests/test_module_artifact.cpp` — round-trip, pub extraction, version mismatch, build dir creation

## 4. Semantic Analyzer Extension

- [ ] 4.1 Define `ImportedSymbols` struct — per-module maps of pub traits/structs/enums/events/funcs/units, keyed by module path or alias, with a global index for uniqueness detection
- [ ] 4.2 Implement qualified symbol resolution — `module.Symbol` and `alias.Symbol` lookups against `ImportedSymbols` per-module maps
- [ ] 4.3 Implement unqualified shortcut — for each unqualified name, check (1) local declarations, (2) uniqueness across all imported modules. If unique → resolve. If ambiguous → error listing conflicting modules with alias suggestion
- [ ] 4.4 Implement filter clause alias resolution — parse `filter: [mod.Trait as alias]` entries, build alias→trait mapping scoped to the system, allow `alias.field` access in system body
- [ ] 4.5 Implement trait field disambiguation — detect overlapping field names across filtered traits, require qualification (trait name or alias) for ambiguous fields, allow unqualified for unique fields
- [ ] 4.6 Implement non-pub helpful error — when qualified lookup fails but symbol exists as non-pub, suggest adding `pub`
- [ ] 4.7 Maintain backward compatibility — existing single-file `analyze(ProgramNode&)` works with empty imports
- [ ] 4.8 Write `tests/test_semantic_modules.cpp` — qualified resolution, alias resolution, unqualified unique/ambiguous, filter aliases, field disambiguation, non-pub error, backward compat

## 5. Program Linker

- [ ] 5.1 Define `src/frontend/program_linker.h` — `ProgramLinker` with `link(artifact_paths)` loading `.cmod` files and returning merged `DecoratedProgram`
- [ ] 5.2 Implement incremental merging — load artifacts one at a time, merge traits/structs/enums/dependency graphs/string pools
- [ ] 5.3 Implement duplicate symbol detection — same pub name from different modules = error
- [ ] 5.4 Implement combined AST construction — declarations in dependency order
- [ ] 5.5 Implement const block merging with duplicate detection
- [ ] 5.6 Write `tests/test_program_linker.cpp` — merge from artifacts, duplicates, ordering, const merging

## 6. CLI Update

- [ ] 6.1 Add `--module-path` flag parsing — repeatable, collect search directories
- [ ] 6.2 Integrate multi-module pipeline — resolve → compile in topo order (write .cmod) → link → pass to backend
- [ ] 6.3 Maintain single-file backward compatibility — skip resolution/linking when no `use` declarations

## 7. Integration Testing

- [ ] 7.1 Create `tests/fixtures/multi_module/` fixture — main uses a and b (with aliases), b uses a, shared traits with field name conflicts
- [ ] 7.2 Write `tests/test_multi_module_integration.cpp` — end-to-end: resolve → compile → artifacts → link → verify merged program
- [ ] 7.3 Add CMakeLists.txt entries — new source files and test targets
