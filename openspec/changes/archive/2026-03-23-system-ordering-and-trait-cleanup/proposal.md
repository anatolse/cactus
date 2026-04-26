## Why

The `trait` declaration currently allows event handlers and functions in its body — a grammatical artefact that contradicts the core ECS philosophy ("traits = data, systems = logic") and is never used in any real code. Separately, there is no mechanism to declare explicit execution ordering between systems: data-flow dependencies can be partially inferred from filter patterns, but ordering that isn't driven by data (e.g., render passes, UI-over-scene layering) is impossible to express today.

## What Changes

- **BREAKING: Remove event handlers and `func` declarations from `trait` bodies.** The `trait_decl` grammar is restricted to `field_decl` only. Any code placing logic inside a trait is now a parse/semantic error.
- **New `after:` clause on `system` declarations.** A system may optionally declare `after: SystemA, SystemB` (comma-separated list of system names). The compiler validates all named systems exist, detects ordering cycles, and stores the constraints in the `DecoratedProgram` dependency graph.
- **Qualified field access in `config:` blocks and `spawn()` overrides.** Config keys and spawn override arguments may use `TraitName.field` or `alias.field` dotted notation in addition to bare field names. Bare names remain valid when unambiguous; ambiguous bare names become a semantic error with a disambiguation hint. The `apply:` block of units and templates optionally supports `as` aliases (same syntax as system `filter:`).
- **Update `spec/cactus_dsl_spec.md`.** Grammar, semantic rules, and examples in the main language spec are updated to reflect all changes.
- **Frontend-only change.** Lexer, parser, AST, and semantic analyzer are updated. No backend code changes.

## Capabilities

### New Capabilities

- `dsl-system-ordering`: The `after:` clause on `system` declarations — syntax, semantics, cycle detection, and dependency graph storage.
- `dsl-unit-config-qualification`: Qualified `TraitName.field` and `alias.field` syntax in `config:` blocks and `spawn()` override arguments; optional `as` aliases in `apply:` blocks.

### Modified Capabilities

- `dsl-parser`: Trait body grammar restricted to fields only; `after:` clause parsing added to system declarations; `apply:` alias parsing added; dotted key syntax added to `config:` and `spawn` override argument lists.
- `dsl-semantic-analysis`: Trait handler validation removed; `after:` system name resolution and cycle detection added; dependency graph extended with explicit ordering edges; qualified config/spawn key resolution and ambiguity detection added.

## Impact

- **`ast.hpp`** — `TraitNode.handlers` field removed.
- **`ast.hpp`** — `SystemNode` gains `std::vector<std::string> after_systems`.
- **`ast.hpp`** — `ApplyEntry` gains `std::optional<std::string> alias`; `ConfigAssignment` key changes from `std::string` to support dotted form.
- **`parser.cpp`** — trait body restricted; system gains `after:` parsing; `apply:` gains optional `as alias`; `config:` and `spawn` args gain `TraitName.field` key parsing.
- **`semantic_analyzer.cpp`** — trait handler validation removed; `after:` name/cycle validation added; qualified config/spawn key resolution and ambiguity detection added.
- **`spec/cactus_dsl_spec.md`** — §3.6, §3.8, §3.8a, §3.9 updated.
- No changes to backends, module artifact, or stdlib files.
- Existing `.cactus` files with bare field names in `config:` and `spawn()` are unaffected (bare names remain valid when unambiguous).
