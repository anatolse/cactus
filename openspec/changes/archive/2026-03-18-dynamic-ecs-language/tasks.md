## 1. `spec/cactus_dsl_spec.md` — Language Spec Update

- [x] 1.1 Add `template`, `spawn`, `destroy`, `load`, `unload`, `enable`, `disable`, `exclude`, `disabled` to keyword table (section 2.4)
- [x] 1.2 Add `template_decl` grammar rule and description (section 3)
- [x] 1.3 Update `system_decl` grammar to show optional `filter:` and `exclude:` blocks with indented-list syntax; `filter:` optional (match-all when absent)
- [x] 1.4 Update `trait_decl` grammar to show optional body (marker trait)
- [x] 1.5 Add `spawn_stmt`, `destroy_stmt`, `load_stmt`, `enable_stmt`, `disable_stmt` to statements section (3.17)
- [x] 1.6 Document all four lifecycle handlers: `on spawn()`, `on destroy()`, `on unload()`, `on load()` with 3-phase load sequence
- [x] 1.7 Document scene loading execution model, `std.core` module, and optional filter/exclude semantics
- [x] 1.8 Document `_data.bin` flat binary data file per module (section 8 / compilation model)

## 2. Lexer — New Keywords

- [x] 2.1 Add `TEMPLATE`, `SPAWN`, `DESTROY`, `LOAD`, `UNLOAD`, `ENABLE`, `DISABLE`, `EXCLUDE`, `DISABLED` token types to `token.hpp`
- [x] 2.2 Register all 9 new keywords in `lexer.cpp` keyword map
- [x] 2.3 Add lexer tests for each new keyword tokenizing correctly
- [x] 2.4 Add lexer tests verifying new keywords are rejected as identifiers

## 3. AST — New Node Types

- [x] 3.1 Add `TemplateDecl` AST node to `ast.hpp` (mirrors `UnitDecl`, adds no extra fields)
- [x] 3.2 Extend `ApplyEntry` AST node to include `initially_active: bool` (default `true`)
- [x] 3.3 Add `SpawnStmt` AST node: `template_name`, `overrides: list<(field_name, expr)>`
- [x] 3.4 Add `DestroyStmt` AST node (no fields)
- [x] 3.5 Add `LoadStmt` AST node: `module_name: string`
- [x] 3.6 Add `EnableStmt` and `DisableStmt` AST nodes: `trait_name: string`
- [x] 3.7 Extend `SystemDecl` AST node to include `exclude: list<TraitRef>` (alongside `filter`); both optional (empty list = not specified)
- [x] 3.8 Extend `EventHandler` AST node to support `spawn`, `destroy`, `load`, `unload` as lifecycle event names

## 4. Parser — Grammar Changes

- [x] 4.1 Parse `template_decl` — same structure as `unit_decl` using `TEMPLATE` keyword
- [x] 4.2 Parse `apply_entry` with optional `: disabled` annotation (`COLON DISABLED`)
- [x] 4.3 Replace bracket `filter: [...]` parser with optional indented-block `filter:` parser; emit error for old bracket syntax
- [x] 4.4 Parse optional `exclude:` indented block on system declarations
- [x] 4.5 Parse `spawn_stmt`: `SPAWN IDENTIFIER LPAREN [spawn_args] RPAREN NEWLINE`
- [x] 4.6 Parse `spawn_arg_list`: comma-separated `IDENTIFIER ASSIGN expression` pairs
- [x] 4.7 Parse `destroy_stmt`: `DESTROY NEWLINE`
- [x] 4.8 Parse `load_stmt`: `LOAD dotted_name NEWLINE`
- [x] 4.9 Parse `enable_stmt` and `disable_stmt`: `ENABLE/DISABLE IDENTIFIER NEWLINE`
- [x] 4.10 Parse `on spawn()`, `on destroy()`, `on load()`, `on unload()` lifecycle handlers with empty param lists
- [x] 4.11 Make trait body optional in `trait_decl` (marker trait: no colon, no body)
- [x] 4.12 Add parser tests for all new grammar rules
- [x] 4.13 Add parser test for old bracket filter syntax producing error

## 5. Semantic Analysis — New Validations

- [x] 5.1 Validate `template` declarations (same rules as `unit`: trait existence, config field membership, `let` defaults)
- [x] 5.2 Track templates in symbol table as spawnable archetypes (separate from `unit` singletons)
- [x] 5.3 Validate `spawn` sites: template exists, override fields valid, required fields provided
- [x] 5.4 Validate `spawn` does not target a `unit` (error: use template only)
- [x] 5.5 Validate `destroy`, `spawn`, `load`, `enable`, `disable` only appear inside system event handlers
- [x] 5.6 Validate `load` module name is reachable via `use` declarations
- [x] 5.7 Validate `enable`/`disable` trait is declared (simplified: check declaration exists)
- [x] 5.8 Validate `on spawn()`, `on destroy()`, `on load()`, `on unload()` handlers have empty parameter lists
- [x] 5.9 Validate `exclude:` trait names are declared
- [x] 5.10 Validate optional `filter:` — no filter is valid (match-all); if filter is present, validate all trait names are declared
- [x] 5.11 Validate `disabled` annotation in `apply:` is on a recognized trait
- [x] 5.12 Add semantic tests for all new validation rules and error messages

## 6. Module Artifact — Data File Emission

- [x] 6.1 Define `_data.bin` flat binary format: magic bytes `CDAT` + uint16 version + uint32 entity count, then sequential entity records (name length + name bytes + fixed-width field values in declaration order + uint64 trait active bitmask); no offset table — single `fread` load
- [x] 6.2 Extend `ModuleArtifact` to carry `unit_instance_data` (serialized list of unit configs)
- [x] 6.3 Implement data file writer: serialize all `unit` declarations' resolved field values and initial trait states
- [x] 6.4 Ensure `template` declarations produce no entries in the data file
- [x] 6.5 Implement data file reader (runtime side): deserialize entity records and instantiate them
- [x] 6.6 Add version header check: reject mismatched format version with clear error
- [x] 6.7 Add tests for data file round-trip (write module with units → read back → verify entity configs match)

## 7. Code Generation — SoA Backend (cpp-manual)

- [x] 7.1 Assign each declared trait a unique compile-time bit index; emit a `TraitBits` enum/constexpr table in generated code
- [x] 7.2 Add `uint64_t trait_mask` field to the SoA entity storage; initialize from `apply:` block (active = bit set, `disabled` = bit clear)
- [x] 7.3 Compile `filter:` to `filter_mask` (0 if no `filter:` clause); emit loop condition as: `(trait_mask & filter_mask) == filter_mask` — when filter_mask=0 this is always true (match all)
- [x] 7.4 Compile `exclude:` to `exclude_mask` (0 if no `exclude:` clause); extend loop condition: `&& (trait_mask & exclude_mask) == 0`
- [x] 7.5 Emit `enable TraitName` as `trait_mask |= TraitBits::TraitName`
- [x] 7.6 Emit `disable TraitName` as `trait_mask &= ~TraitBits::TraitName`
- [x] 7.7 Emit `template` declarations as C++ factory functions that fill a new entity slot with default field values + spawn overrides + initial `trait_mask`
- [x] 7.8 Emit `spawn TemplateName(overrides)` as a call to the template factory function, appending at `entity_count` and incrementing count
- [x] 7.9 Emit `destroy` using swap-and-delete: copy last-entity data into current slot, decrement `entity_count` (do not iterate forward after destroy in same loop)
- [x] 7.10 Emit `load` as: store pending module name in a deferred-load slot; emit no immediate code (executed at frame end by runtime)
- [x] 7.11 Emit `on spawn()` dispatch: after factory function fills slot, call all system handlers whose `filter_mask` matches the new entity's `trait_mask`
- [x] 7.12 Emit `on destroy()` dispatch: before swap-and-delete, call all system handlers whose filter matches the entity being removed
- [x] 7.13 Emit `on unload()` dispatch: at start of scene transition, iterate all systems and call `on_unload()` handler (running filtered/excluded entity loop for each)
- [x] 7.14 Emit `on load()` dispatch: after scene transition completes, iterate all systems and call their `on_load()` handler if present
- [x] 7.15 Add codegen tests verifying bitmask constants are correct, loop condition handles optional filter/exclude, spawn appends entity, destroy uses swap-and-delete, on unload/load dispatch order

## 8. Scene Loading Runtime

- [x] 8.1 Implement end-of-frame deferred load mechanism: buffer at most one `load` call per frame; if a second `load` is registered in the same frame, report a runtime error: "multiple `load` calls in a single frame"
- [x] 8.2 **Phase 1 — Unload**: emit `on unload()` to all active systems (in declaration order); systems like `std.SceneCleanup` destroy entities here via swap-and-delete
- [x] 8.3 **Phase 2 — Instantiate**: read target module's `_data.bin`; instantiate entities with field values and initial trait bitmask; emit `on spawn()` per entity
- [x] 8.4 **Phase 3 — Load**: emit `on load()` to all active systems; level-setup systems spawn templates and configure state
- [x] 8.5 Verify `on unload()` runs before any new entities are created (no interaction between old teardown and new entities)
- [x] 8.6 Add integration tests: 3-phase ordering correct, std.SceneCleanup destroys non-persistent on unload, new entities exist during on load

## 9. Standard Library — `std/core.cactus`

- [x] 9.1 Create `std/core.cactus` containing `pub trait Persistent` and `pub system SceneCleanup` (exclude: Persistent, on unload: destroy)
- [x] 9.2 Add `std` to the compiler's built-in module search path (alongside `--module-path` user paths)
- [x] 9.3 Verify `use std.core` in user files compiles, imports `Persistent` and `SceneCleanup` correctly
- [x] 9.4 Add integration test: without `use std.core`, `load` does not destroy any entities; with `use std.core`, non-persistent entities are destroyed on unload
- [x] 9.5 Update `examples/platformer/platformer.cactus` to `use std.core` and apply `Persistent` to `Player`

## 10. Example Migration — Breaking Filter Syntax

- [x] 10.1 Migrate `examples/platformer/platformer.cactus` — convert all `filter: [...]` to indented block form
- [x] 10.2 Migrate `examples/platformer/level.cactus`, `player.cactus`, `enemies.cactus`, `collectibles.cactus`, `camera.cactus`, `ui.cactus`
- [x] 10.3 Migrate `examples/cactus_shop/` — all files using `filter: [...]`
- [x] 10.4 Migrate all test fixture `.cactus` files in `tests/fixtures/` using old filter syntax
- [x] 10.5 Verify all examples and tests compile and pass after migration

## 11. Integration Tests

- [x] 11.1 Add test: `template` declared, not auto-instantiated
- [x] 11.2 Add test: `spawn` creates entity, `on spawn()` fires, fields initialized
- [x] 11.3 Add test: `destroy` removes entity, `on destroy()` fires with fields still readable
- [x] 11.4 Add test: `load` transition — 3-phase: on unload fires, non-persistent removed, new entities spawned, on load fires
- [x] 11.5 Add test: `on unload()` fires before new entities are created (3-phase ordering)
- [x] 11.6 Add test: `on load()` fires after all entities instantiated
- [x] 11.7 Add test: `enable`/`disable` toggles filter visibility; field data preserved
- [x] 11.8 Add test: `exclude:` skips entities with active excluded trait
- [x] 11.9 Add test: `exclude:` with disabled trait — entity still processed
- [x] 11.10 Add test: marker trait (no body) accepted in `apply:`, `filter:`, `exclude:`
- [x] 11.11 Add test: system with no `filter:` + `exclude: Persistent` processes all non-persistent entities
- [x] 11.12 Add test: field access in no-filter system body is a compile error
- [x] 11.13 Add test: `Persistent` entity survives `load`; entity without `Persistent` is removed (via std.SceneCleanup)
