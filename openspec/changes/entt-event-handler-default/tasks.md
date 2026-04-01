## 1. Backend: cpp-entt — Event Handler Codegen

> **Prerequisite**: The `dsl-event-handler-syntax` change must be fully applied before starting these tasks. `EventHandlerNode` must have `alias: optional<string>` and no `params`.

- [ ] 1.1 Update `emit_system()` in `src/backends/cpp-entt/system_emitter.cpp`: replace the hardcoded `if (handler.event_name == "tick" || ...)  out << ", float dt";` block with `out << ", const " << handler.event_name << "Event& " << handler.alias.value_or(handler.event_name);` for all handlers
- [ ] 1.2 Remove the `MemberExpr` translation in `rewrite_expr()` that converts `tick.dt` / `fixed_tick.dt` / `late_tick.dt` to bare `dt` — emit the member access as-is (the event variable is now in scope as a parameter)

## 2. Tests: cpp-entt Codegen

- [ ] 2.1 Update `tests/test_codegen_entt.cpp`: for all event handler test cases, update expected C++ output to use `const TickEvent& tick` (and `tick.dt`) instead of `float dt` (and bare `dt`)
- [ ] 2.2 Add a test case in `test_codegen_entt.cpp` for an aliased handler `on tick as t:` that verifies the generated signature uses `const TickEvent& t` and body references `t.dt`
- [ ] 2.3 Add a test case for a marker lifecycle event handler `on spawn:` that verifies the generated signature uses `const SpawnEvent& spawn`

## 3. Backend: cpp-manual — Event Handler Codegen

- [ ] 3.1 Update `src/backends/cpp-manual/event_emitter.cpp`: remove the hardcoded lifecycle event struct list; generate lifecycle event structs by iterating over the resolved `EventNode` declarations from std.core (same as user-defined events)
- [ ] 3.2 Update `src/backends/cpp-manual/system_emitter.cpp`: for each event handler, emit `const EventType& <name>` as the handler callback parameter (using `handler.alias.value_or(handler.event_name)`); remove old per-field parameter emission

## 4. Tests: cpp-manual Codegen

- [ ] 4.1 Update `tests/test_codegen_manual.cpp`: update expected C++ output for all event handler tests to use the new `const TickEvent& tick` pattern and `tick.dt` field access
- [ ] 4.2 Add a test case for an aliased handler `on tick as t:` that verifies `const TickEvent& t` signature
- [ ] 4.3 Add a test case for marker event `on spawn:` verifying `const SpawnEvent& spawn` signature and empty `SpawnEvent` struct

## 5. CLI Default Backend

- [ ] 5.1 Update `src/main.cpp`: change the default value string for the `--backend` argument from `"cpp-manual"` to `"cpp-entt"`
