## 1. AST Changes

- [ ] 1.1 Update `EventHandlerNode` in `src/frontend/ast.h`: remove `params` field, add `alias: optional<string>` field

## 2. Parser

- [ ] 2.1 Update `parseEventHandler()` in `parser.cpp`: remove parameter list parsing, add optional `as IDENTIFIER` alias clause parsing
- [ ] 2.2 Add parse error when `on event_name(` is encountered: "unexpected '('; event handlers no longer take a parameter list; use 'on tick:' and access fields as 'tick.dt'"
- [ ] 2.3 Update `parseEventDecl()` in `parser.cpp`: make the colon+body block optional (marker event support, consistent with marker trait pattern)
- [ ] 2.4 Update `test_parser.cpp`: convert all lifecycle handler test cases to new no-param syntax, add alias parsing tests, add test for old param syntax producing error, add marker event parsing test

## 3. Semantic Analyzer

- [ ] 3.1 Remove the hardcoded lifecycle event signature validation table from `semantic_analyzer.cpp` (the table mapping handler names to expected parameter types)
- [ ] 3.2 Implement unified implicit event variable binding: when entering any event handler body, introduce a read-only local variable named after the event (or alias if present), typed as the resolved event struct
- [ ] 3.3 Remove the `event` implicit object injection for user event handlers; all handlers now use the event-name binding from 3.2
- [ ] 3.4 Add validation: reject handler alias that conflicts with an existing filter alias or other in-scope name; report "handler alias '<name>' conflicts with filter alias '<name>' already in scope"
- [ ] 3.5 Update `test_semantic.cpp` and `test_semantic_dynamic.cpp`: update event handler tests to new syntax, remove lifecycle signature validation tests, add tests for event-name binding, alias binding, read-only enforcement, and conflict detection

## 4. Stdlib — Standard Event Declarations

- [ ] 4.1 Add lifecycle event declarations to `stdlib/std/core.cactus`: `pub event tick:` (dt: float), `pub event fixed_tick:` (dt: float), `pub event late_tick:` (dt: float), and marker events `pub event spawn`, `pub event destroy`, `pub event input`, `pub event load`, `pub event unload`
- [ ] 4.2 Update `SceneCleanup` system in `stdlib/std/core.cactus`: change `on unload():` to `on unload:`

> **Note:** The cpp-entt backend is **out of scope** for this change. `test_codegen_entt.cpp` is not modified; the entt backend's event handler codegen remains unchanged until a future change targets it.

## 5. Backend: cpp-manual

- [ ] 5.1 Update `src/backends/cpp-manual/event_emitter.cpp`: generate lifecycle event structs (`struct TickEvent { float dt; };` etc.) from the std.core event declarations (not from a hardcoded list)
- [ ] 5.2 Update `src/backends/cpp-manual/system_emitter.cpp`: for each event handler, emit `const EventType& <name>` as the handler callback parameter (using alias if present, otherwise event name); remove old per-field parameter emission
- [ ] 5.3 Update `test_codegen_manual.cpp`: update expected C++ output for all event handler tests to use the new `const TickEvent& tick` pattern and `tick.dt` field access

## 6. Migrate .cactus Source Files

- [ ] 6.1 Update all event handlers in `examples/platformer/` files (player, enemies, collectibles, camera, ui, level, platformer): change `on tick(dt: float):` → `on tick:`, replace `dt` with `tick.dt`; change `on EventName():` → `on EventName:`, replace `event.field` with `EventName.field`
- [ ] 6.2 Update all event handlers in `examples/cactus_shop/` files: same migration as above
- [ ] 6.3 Update `examples/dsl_showcase.cactus`: migrate all event handler syntax
- [ ] 6.4 Update test fixture files in `tests/fixtures/` (simple_system, cactus_shop_mini, minimal_trait, multi_module files): migrate all event handler syntax
- [ ] 6.5 Update any remaining stdlib module files that contain event handlers (transform, physics, camera, render, audio)
