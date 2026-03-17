## 1. `spec/cactus_dsl_spec.md` — Language Spec Update

- [ ] 1.1 Add `template`, `spawn`, `destroy`, `load`, `unload`, `enable`, `disable`, `exclude`, `disabled` to keyword table (section 2.4)
- [ ] 1.2 Add `template_decl` grammar rule and description (section 3)
- [ ] 1.3 Update `system_decl` grammar to show optional `filter:` and `exclude:` blocks with indented-list syntax; `filter:` optional (match-all when absent)
- [ ] 1.4 Update `trait_decl` grammar to show optional body (marker trait)
- [ ] 1.5 Add `spawn_stmt`, `destroy_stmt`, `load_stmt`, `enable_stmt`, `disable_stmt` to statements section (3.17)
- [ ] 1.6 Document all four lifecycle handlers: `on spawn()`, `on destroy()`, `on unload()`, `on load()` with 3-phase load sequence
- [ ] 1.7 Document scene loading execution model, `std.core` module, and optional filter/exclude semantics
- [ ] 1.8 Document `_data.bin` flat binary data file per module (section 8 / compilation model)

## 2. Lexer — New Keywords

- [ ] 2.1 Add `TEMPLATE`, `SPAWN`, `DESTROY`, `LOAD`, `UNLOAD`, `ENABLE`, `DISABLE`, `EXCLUDE`, `DISABLED` token types to `token.h`
- [ ] 2.2 Register all 9 new keywords in `lexer.cpp` keyword map
- [ ] 2.3 Add lexer tests for each new keyword tokenizing correctly
- [ ] 2.4 Add lexer tests verifying new keywords are rejected as identifiers

## 3. AST — New Node Types

- [ ] 3.1 Add `TemplateDecl` AST node to `ast.h` (mirrors `UnitDecl`, adds no extra fields)
- [ ] 3.2 Extend `ApplyEntry` AST node to include `initially_active: bool` (default `true`)
- [ ] 3.3 Add `SpawnStmt` AST node: `template_name`, `overrides: list<(field_name, expr)>`
- [ ] 3.4 Add `DestroyStmt` AST node (no fields)
- [ ] 3.5 Add `LoadStmt` AST node: `module_name: string`
- [ ] 3.6 Add `EnableStmt` and `DisableStmt` AST nodes: `trait_name: string`
- [ ] 3.7 Extend `SystemDecl` AST node to include `exclude: list<TraitRef>` (alongside `filter`); both optional (empty list = not specified)
- [ ] 3.8 Extend `EventHandler` AST node to support `spawn`, `destroy`, `load`, `unload` as lifecycle event names

## 4. Parser — Grammar Changes

- [ ] 4.1 Parse `template_decl` — same structure as `unit_decl` using `TEMPLATE` keyword
- [ ] 4.2 Parse `apply_entry` with optional `: disabled` annotation (`COLON DISABLED`)
- [ ] 4.3 Replace bracket `filter: [...]` parser with optional indented-block `filter:` parser; emit error for old bracket syntax
- [ ] 4.4 Parse optional `exclude:` indented block on system declarations
- [ ] 4.5 Parse `spawn_stmt`: `SPAWN IDENTIFIER LPAREN [spawn_args] RPAREN NEWLINE`
- [ ] 4.6 Parse `spawn_arg_list`: comma-separated `IDENTIFIER ASSIGN expression` pairs
- [ ] 4.7 Parse `destroy_stmt`: `DESTROY NEWLINE`
- [ ] 4.8 Parse `load_stmt`: `LOAD dotted_name NEWLINE`
- [ ] 4.9 Parse `enable_stmt` and `disable_stmt`: `ENABLE/DISABLE IDENTIFIER NEWLINE`
- [ ] 4.10 Parse `on spawn()`, `on destroy()`, `on load()`, `on unload()` lifecycle handlers with empty param lists
- [ ] 4.11 Make trait body optional in `trait_decl` (marker trait: no colon, no body)
- [ ] 4.12 Add parser tests for all new grammar rules
- [ ] 4.13 Add parser test for old bracket filter syntax producing error

## 5. Semantic Analysis — New Validations

- [ ] 5.1 Validate `template` declarations (same rules as `unit`: trait existence, config field membership, `let` defaults)
- [ ] 5.2 Track templates in symbol table as spawnable archetypes (separate from `unit` singletons)
- [ ] 5.3 Validate `spawn` sites: template exists, override fields valid, required fields provided
- [ ] 5.4 Validate `spawn` does not target a `unit` (error: use template only)
- [ ] 5.5 Validate `destroy`, `spawn`, `load`, `enable`, `disable` only appear inside system event handlers
- [ ] 5.6 Validate `load` module name is reachable via `use` declarations
- [ ] 5.7 Validate `enable`/`disable` trait is present in `apply:` of at least one archetype matching the system filter
- [ ] 5.8 Validate `on spawn()`, `on destroy()`, `on load()`, `on unload()` handlers have empty parameter lists
- [ ] 5.9 Validate `exclude:` trait names are declared
- [ ] 5.10 Validate optional `filter:` — no filter is valid (match-all); if filter is present, validate all trait names are declared; validate field access in handler body requires trait to be in `filter:`
- [ ] 5.11 Validate `disabled` annotation in `apply:` is on a recognized trait
- [ ] 5.12 Add semantic tests for all new validation rules and error messages

## 6. Module Artifact — Data File Emission

- [ ] 6.1 Define `_data.bin` flat binary format: magic bytes `CDAT` + uint16 version + uint32 entity count, then sequential entity records (name length + name bytes + fixed-width field values in declaration order + uint64 trait active bitmask); no offset table — single `fread` load
- [ ] 6.2 Extend `ModuleArtifact` to carry `unit_instance_data` (serialized list of unit configs)
- [ ] 6.3 Implement data file writer: serialize all `unit` declarations' resolved field values and initial trait states
- [ ] 6.4 Ensure `template` declarations produce no entries in the data file
- [ ] 6.5 Implement data file reader (runtime side): deserialize entity records and instantiate them
- [ ] 6.6 Add version header check: reject mismatched format version with clear error
- [ ] 6.7 Add tests for data file round-trip (write module with units → read back → verify entity configs match)

## 7. Code Generation — SoA Backend (cpp-manual)

- [ ] 7.1 Assign each declared trait a unique compile-time bit index; emit a `TraitBits` enum/constexpr table in generated code
- [ ] 7.2 Add `uint64_t trait_mask` field to the SoA entity storage; initialize from `apply:` block (active = bit set, `disabled` = bit clear)
- [ ] 7.3 Compile `filter:` to `filter_mask` (0 if no `filter:` clause); emit loop condition as: `(trait_mask & filter_mask) == filter_mask` — when filter_mask=0 this is always true (match all)
- [ ] 7.4 Compile `exclude:` to `exclude_mask` (0 if no `exclude:` clause); extend loop condition: `&& (trait_mask & exclude_mask) == 0`
- [ ] 7.5 Emit `enable TraitName` as `trait_mask |= TraitBits::TraitName`
- [ ] 7.6 Emit `disable TraitName` as `trait_mask &= ~TraitBits::TraitName`
- [ ] 7.7 Emit `template` declarations as C++ factory functions that fill a new entity slot with default field values + spawn overrides + initial `trait_mask`
- [ ] 7.8 Emit `spawn TemplateName(overrides)` as a call to the template factory function, appending at `entity_count` and incrementing count
- [ ] 7.9 Emit `destroy` using swap-and-delete: copy last-entity data into current slot, decrement `entity_count` (do not iterate forward after destroy in same loop)
- [ ] 7.10 Emit `load` as: store pending module name in a deferred-load slot; emit no immediate code (executed at frame end by runtime)
- [ ] 7.11 Emit `on spawn()` dispatch: after factory function fills slot, call all system handlers whose `filter_mask` matches the new entity's `trait_mask`
- [ ] 7.12 Emit `on destroy()` dispatch: before swap-and-delete, call all system handlers whose filter matches the entity being removed
- [ ] 7.13 Emit `on unload()` dispatch: at start of scene transition, iterate all systems and call `on_unload()` handler (running filtered/excluded entity loop for each)
- [ ] 7.14 Emit `on load()` dispatch: after scene transition completes, iterate all systems and call their `on_load()` handler if present
- [ ] 7.15 Add codegen tests verifying bitmask constants are correct, loop condition handles optional filter/exclude, spawn appends entity, destroy uses swap-and-delete, on unload/load dispatch order

## 8. Scene Loading Runtime

- [ ] 8.1 Implement end-of-frame deferred load mechanism: buffer at most one `load` call per frame; if a second `load` is registered in the same frame, report a runtime error: "multiple `load` calls in a single frame"
- [ ] 8.2 **Phase 1 — Unload**: emit `on unload()` to all active systems (in declaration order); systems like `std.SceneCleanup` destroy entities here via swap-and-delete
- [ ] 8.3 **Phase 2 — Instantiate**: read target module's `_data.bin`; instantiate entities with field values and initial trait bitmask; emit `on spawn()` per entity
- [ ] 8.4 **Phase 3 — Load**: emit `on load()` to all active systems; level-setup systems spawn templates and configure state
- [ ] 8.5 Verify `on unload()` runs before any new entities are created (no interaction between old teardown and new entities)
- [ ] 8.6 Add integration tests: 3-phase ordering correct, std.SceneCleanup destroys non-persistent on unload, new entities exist during on load

## 9. Standard Library — `std/core.cactus`

- [ ] 9.1 Create `std/core.cactus` containing `pub trait Persistent` and `pub system SceneCleanup` (exclude: Persistent, on unload: destroy)
- [ ] 9.2 Add `std` to the compiler's built-in module search path (alongside `--module-path` user paths)
- [ ] 9.3 Verify `use std.core` in user files compiles, imports `Persistent` and `SceneCleanup` correctly
- [ ] 9.4 Add integration test: without `use std.core`, `load` does not destroy any entities; with `use std.core`, non-persistent entities are destroyed on unload
- [ ] 9.5 Update `examples/platformer/platformer.cactus` to `use std.core` and apply `std.Persistent` to `Player`

## 10. Example Migration — Breaking Filter Syntax

- [ ] 10.1 Migrate `examples/platformer/platformer.cactus` — convert all `filter: [...]` to indented block form
- [ ] 10.2 Migrate `examples/platformer/level.cactus`, `player.cactus`, `enemies.cactus`, `collectibles.cactus`, `camera.cactus`, `ui.cactus`
- [ ] 10.3 Migrate `examples/cactus_shop/` — all files using `filter: [...]`
- [ ] 10.4 Migrate all test fixture `.cactus` files in `tests/fixtures/` using old filter syntax
- [ ] 10.5 Verify all examples and tests compile and pass after migration

## 11. Integration Tests

- [ ] 11.1 Add test: `template` declared, not auto-instantiated
- [ ] 11.2 Add test: `spawn` creates entity, `on spawn()` fires, fields initialized
- [ ] 11.3 Add test: `destroy` removes entity, `on destroy()` fires with fields still readable
- [ ] 11.4 Add test: `load` transition — 3-phase: on unload fires, non-persistent removed, new entities spawned, on load fires
- [ ] 11.5 Add test: `on unload()` fires before new entities are created (3-phase ordering)
- [ ] 11.6 Add test: `on load()` fires after all entities instantiated
- [ ] 11.7 Add test: `enable`/`disable` toggles filter visibility; field data preserved
- [ ] 11.8 Add test: `exclude:` skips entities with active excluded trait
- [ ] 11.9 Add test: `exclude:` with disabled trait — entity still processed
- [ ] 11.10 Add test: marker trait (no body) accepted in `apply:`, `filter:`, `exclude:`
- [ ] 11.11 Add test: system with no `filter:` + `exclude: Persistent` processes all non-persistent entities
- [ ] 11.12 Add test: field access in no-filter system body is a compile error
- [ ] 11.13 Add test: `Persistent` entity survives `load`; entity without `Persistent` is removed (via std.SceneCleanup)
