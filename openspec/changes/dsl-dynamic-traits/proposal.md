## Why

The current trait system uses `enable`/`disable` to toggle traits at runtime, but the trait set itself is fixed at compile time via `apply:`. This prevents truly dynamic composition — you cannot attach a new trait to an entity that wasn't declared in its `apply:` block. Real gameplay patterns like power-ups, status effects, and emergent states require open-world component mutation. Backward compatibility is not required.

## What Changes

- **BREAKING**: Remove `enable` and `disable` statements from the DSL
- **BREAKING**: Remove the `: disabled` annotation from `apply:` block entries
- Add `add TraitName` statement for runtime trait attachment — marker traits and all-defaulted traits use bare form; data traits with required fields use block syntax (`add TraitName:` followed by indented field assignments); cross-entity targeting via `add TraitName to expr:` 
- Add `remove TraitName` statement for runtime trait detachment — bare form destroys the trait from self; cross-entity targeting via `remove TraitName from expr`
- Trait field declarations gain support for default values (`var duration: float = 3.0`); traits where all fields have defaults can be `add`ed without arguments
- `apply:` block becomes a simple list of traits to add at spawn time — no enable/disable annotations
- `filter:` and `exclude:` become pure runtime queries (open world); the compiler still validates trait names but cannot prove component presence statically

## Capabilities

### New Capabilities
- `dsl-dynamic-traits`: The `add`/`remove` statement pair for runtime trait attachment/detachment, where `add` uses block syntax (colon + indented field assignments) for data traits, cross-entity targeting via `to`/`from` suffixes, and default field values on trait declarations

### Modified Capabilities
- `dsl-trait-modifiers`: Requirements for `enable`, `disable`, and `: disabled` are removed; `filter:` and `exclude:` semantics updated to open-world runtime queries; `apply:` annotation changed — no `initially_active` flag
- `dsl-semantic-analysis`: Static validation rules for `enable`/`disable` removed; new validation rules for `add`/`remove` (trait name resolution, required fields without defaults must be supplied, `to`/`from` target must be `entity_id`)
- `dsl-lexer`: Remove `enable` and `disable` keywords; add `add`, `remove`, `to`, `from` as keywords (or context-sensitive keywords)
- `dsl-parser`: Remove `enable_stmt` and `disable_stmt` productions; add `add_stmt` and `remove_stmt` productions where `add` supports block syntax for field initialization
- `backend-cpp-entt`: Code generation for `add`/`remove` maps to `emplace_or_replace<T>()` / `remove<T>()`; no longer generates enable/disable mask operations

## Impact

- `src/frontend/ast.h`: Remove `EnableStmt`, `DisableStmt`; add `AddTraitStmt` and `RemoveTraitStmt` with field-assignment list support; remove `ApplyEntry.initially_active`
- `src/frontend/lexer.cpp/h`: Keyword table changes
- `src/frontend/parser.cpp`: Remove `enable`/`disable` parsing; add `add`/`remove` parsing
- `src/frontend/semantic_analyzer.cpp/h`: Remove enable/disable validation; add add/remove validation
- `src/backends/cpp-entt/`: Codegen changes for add/remove statements
- `src/backends/cpp-manual/`: Codegen changes for add/remove statements
- All example `.cactus` files that use `enable`/`disable` or `: disabled` require migration
- Test files: `test_semantic.cpp`, `test_semantic_dynamic.cpp`, `test_parser.cpp`, `test_codegen_entt.cpp`, `test_codegen_manual.cpp`
