## Context

The `dsl-event-handler-syntax` change (now pending implementation) introduced the unified `on event_name [as alias]:` handler syntax, moved lifecycle events into `std.core`, and updated the AST (`EventHandlerNode.alias` replaces `params`) — but explicitly deferred **both** C++ backends. As a result, after that change lands:

- `src/backends/cpp-entt/system_emitter.cpp` still emits `float dt` for lifecycle handlers and translates `tick.dt` → `dt` in `rewrite_expr()`
- `src/backends/cpp-manual/event_emitter.cpp` and `src/backends/cpp-manual/system_emitter.cpp` still use a hardcoded lifecycle event struct list and per-field parameter emission

Both backends need to be updated to match the new AST shape, and `cpp-entt` should become the CLI default.

## Goals / Non-Goals

**Goals:**
- Update `cpp-entt` event handler codegen to emit `const EventType& <name>` (or alias) as the handler function parameter; remove `float dt` injection and `tick.dt → dt` translation
- Update `cpp-manual` event handler codegen to emit `const EventType& <name>` (or alias); generate lifecycle event structs from the std.core AST event declarations instead of a hardcoded list
- Change the CLI default backend from `cpp-manual` to `cpp-entt`
- Update both `test_codegen_entt.cpp` and `test_codegen_manual.cpp`

**Non-Goals:**
- Changing game-loop wiring in either backend — backends still special-case lifecycle event names for hook-up; only the handler function signature changes
- Updating any `.cactus` source files (handled by `dsl-event-handler-syntax`)
- Changing event struct generation for user-defined events (already driven by `EventNode` fields)

## Decisions

### Entt handler signature mirrors cpp-manual
The generated C++ function becomes:
```cpp
void SystemName_tick(entt::registry& registry, const TickEvent& tick) { ... }
```
`<name>` is `handler.alias.value_or(handler.event_name)` — the same rule used in cpp-manual.

*Alternative considered*: Keep the `float dt` parameter for lifecycle events and only change user-event handlers. Rejected — inconsistent and defeats the purpose of unification.

### Remove `tick.dt` → `dt` translation from `rewrite_expr()`
The `MemberExpr` branch that translates `tick.dt` / `fixed_tick.dt` / `late_tick.dt` to bare `dt` is deleted. After `dsl-event-handler-syntax` lands, `tick.dt` is the canonical form in the AST and should be emitted as-is in the generated C++, because the `const TickEvent& tick` parameter is now in scope.

*Alternative considered*: Keep the translation and add a complementary reverse translation. Rejected — unnecessary complexity; the raw member access is correct.

### CLI default changes in `src/main.cpp` only
The default string literal for `--backend` in `src/main.cpp` changes from `"cpp-manual"` to `"cpp-entt"`. No other code changes — the flag still accepts both values.

## Risks / Trade-offs

- **[Ordering dependency on dsl-event-handler-syntax]** This change assumes the AST has `EventHandlerNode.alias` and no `params`. If implemented before `dsl-event-handler-syntax` lands, it will break the entt backend for existing code.
  → Mitigation: Implement after `dsl-event-handler-syntax` is fully applied. The tasks are sequenced accordingly.

- **[Default backend switch affects existing users]** Anyone running `cactus file.cactus` without `--backend` will now get cpp-entt output instead of cpp-manual output.
  → Mitigation: Both backends produce semantically equivalent output; no `.cactus` syntax changes are required. The `--backend cpp-manual` flag continues to work.

- **[test_codegen_entt.cpp expected output update]** All test cases that check generated C++ for event handlers must be updated to use `const TickEvent& tick` / `tick.dt` patterns.
  → Mitigation: Straightforward string-pattern updates; no test logic changes.
