## 1. AST Changes

- [x] 1.1 Update `EventHandlerNode` in `src/frontend/ast.hpp`: remove `params` field, add `alias: optional<string>` field

## 2. Parser

- [x] 2.1 Update `parseEventHandler()` in `parser.cpp`: remove parameter list parsing entirely, add optional `as IDENTIFIER` alias clause parsing
- [x] 2.2 Update `parseEventDecl()` in `parser.cpp`: make the colon+body block optional (marker event support, consistent with marker trait pattern)
- [x] 2.3 Update `test_parser.cpp`: convert all lifecycle handler test cases to new no-param syntax, add alias parsing tests, add marker event parsing test

## 3. Semantic Analyzer

- [x] 3.1 Remove the hardcoded lifecycle event signature validation table from `semantic_analyzer.cpp` (the table mapping handler names to expected parameter types)
- [x] 3.2 Implement unified implicit event variable binding: when entering any event handler body, introduce a read-only local variable named after the event (or alias if present), typed as the resolved event struct
- [x] 3.3 Remove the `event` implicit object injection for user event handlers; all handlers now use the event-name binding from 3.2
- [x] 3.4 Add validation: reject handler alias that conflicts with an existing filter alias or other in-scope name; report "handler alias '<name>' conflicts with filter alias '<name>' already in scope"
- [x] 3.5 Update `test_semantic.cpp` and `test_semantic_dynamic.cpp`: update event handler tests to new syntax, remove lifecycle signature validation tests, add tests for event-name binding, alias binding, read-only enforcement, and conflict detection

## 4. Stdlib — Standard Event Declarations

- [x] 4.1 Add lifecycle event declarations to `stdlib/std/core.cactus`: `pub event tick:` (dt: float), `pub event fixed_tick:` (dt: float), `pub event late_tick:` (dt: float), and marker events `pub event spawn`, `pub event destroy`, `pub event input`, `pub event load`, `pub event unload`
- [x] 4.2 Update `SceneCleanup` system in `stdlib/std/core.cactus`: change `on unload():` to `on unload:`

> **Note:** Both C++ backends (`cpp-manual` and `cpp-entt`) are **out of scope** for this change. Backend codegen updates are handled in a separate change. `test_codegen_manual.cpp` and `test_codegen_entt.cpp` are not modified here.

## 5. Migrate .cactus Source Files

- [x] 5.1 Update all event handlers in `examples/platformer/` files (player, enemies, collectibles, camera, ui, level, platformer): change `on tick(dt: float):` → `on tick:`, replace `dt` with `tick.dt`; change `on EventName():` → `on EventName:`, replace `event.field` with `EventName.field`
- [x] 5.2 Update all event handlers in `examples/cactus_shop/` files: same migration as above
- [x] 5.3 Update `examples/dsl_showcase.cactus`: migrate all event handler syntax
- [x] 5.4 Update test fixture files in `tests/fixtures/` (simple_system, cactus_shop_mini, minimal_trait, multi_module files): migrate all event handler syntax
- [x] 5.5 Update any remaining stdlib module files that contain event handlers (transform, physics, camera, render, audio)
