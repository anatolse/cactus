# cpp-source-lint-suppressions Specification

## Purpose
TBD - Update Purpose after archive

## Requirements

### Requirement: clang-tidy header filter matches this repo's headers
`.clang-tidy`'s `HeaderFilterRegex` SHALL match the file extension actually used by headers under `src/` and `backends/`, so header-only content is included in diagnostics rather than silently skipped.

#### Scenario: Header filter matches .hpp files
- **WHEN** `.clang-tidy`'s `HeaderFilterRegex` is evaluated against a header path under `src/` (e.g. `src/backends/cpp-entt/raylib_io.hpp`)
- **THEN** the regex SHALL match, given every header in this repository uses the `.hpp` extension

### Requirement: Cognitive-complexity suppressions are reviewed and justified, not accumulated by default
A `// NOLINT(readability-function-cognitive-complexity)` or `// NOLINTNEXTLINE(readability-function-cognitive-complexity)` in the compiler's own source (`src/common`, `src/frontend`, `src/backends`, excluding NOLINT text embedded in generated-output string literals) SHALL remain only where the flagged function's complexity is inherent — genuine exhaustive dispatch over an AST variant type, or logic with no extractable substructure — and SHALL NOT remain where the complexity is attributable to a half-applied extraction pattern, duplicated logic also present elsewhere, or a flat lookup expressible as a data table.

#### Scenario: A suppression covering inherent exhaustive dispatch is retained
- **WHEN** a flagged function's complexity comes from an `if constexpr`/`std::visit` dispatch over an AST variant type, where each arm is already minimal and self-contained
- **THEN** the `NOLINT` SHALL remain, with a trailing comment explaining why the dispatch cannot be reduced further

#### Scenario: A suppression covering a half-applied extraction pattern is resolved
- **WHEN** a flagged dispatch function has some branches already delegating to named helper functions and other branches of the same kind still inlined
- **THEN** the remaining branches SHALL be extracted into named helpers following the function's own existing pattern, and the `NOLINT` SHALL be removed once the function is under the complexity threshold

#### Scenario: A suppression covering duplicated logic is resolved via deduplication
- **WHEN** a flagged function's complexity is substantially attributable to logic that is also implemented, in full or in near-duplicate form, elsewhere in the source tree
- **THEN** the duplicated logic SHALL be extracted into one shared implementation used by all call sites, and the `NOLINT` SHALL be removed from any call site that drops under the complexity threshold as a result

### Requirement: Execution-graph scheduling has a single implementation
The handler-scheduling algorithm — conflict-edge detection between co-triggered handlers, cycle detection over the handler dependency graph, and per-activation topological leveling — SHALL be implemented once and used by both single-module semantic analysis and cross-module program linking, rather than independently duplicated in `semantic_analyzer.cpp` and `program_linker.cpp`.

#### Scenario: Single-module and linked builds report a handler cycle exactly once
- **WHEN** a handler dependency cycle exists among handlers sharing the same trigger, whether detected during single-module semantic analysis or during cross-module linking
- **THEN** the compiler SHALL report that cycle exactly once, not once per graph traversal that reaches it

#### Scenario: Scheduling behavior is unchanged for existing accepted programs
- **WHEN** an existing `.cactus` program that compiles successfully today (per `handler-execution-graph` and `program-linker` scenario coverage) is compiled after the scheduling algorithm is unified
- **THEN** it SHALL still compile successfully, with the same schedule edges, dependency levels, and topological order as before

### Requirement: Trait-override-assignment validation has a single implementation
Validating a set of trait-override field assignments against a resolved template's fields (unknown-trait detection, unknown-field detection, required-field coverage) SHALL be implemented once and shared by every call site that performs this validation (template unit declarations, child archetypes, child override trees, template-backed entity overrides, spawn statements, and spawn expressions), rather than independently duplicated at each site.

#### Scenario: Validation behavior is identical across all call sites
- **WHEN** the same invalid trait-override assignment (e.g. an unknown field for a known trait) is written in a template unit, a child archetype override, and a `spawn` statement
- **THEN** the compiler SHALL report the same class of error at each site, produced by the same shared validation logic
