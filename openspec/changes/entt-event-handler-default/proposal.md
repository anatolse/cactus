## Why

The `dsl-event-handler-syntax` change introduced the unified `on event_name [as alias]:` handler syntax, declared lifecycle events in `std.core`, and updated the AST — but deferred both C++ backends. This change applies the codegen update to both `cpp-entt` and `cpp-manual`, completing the full stack, and promotes `cpp-entt` (the intended production backend with native dispatcher support) to be the CLI default.

## What Changes

- **BREAKING** `backend-cpp-entt` event handler codegen updated: system event handlers now receive `const EventType& <name>` (or alias) instead of the old hardcoded `float dt` parameter; body references use `tick.dt` not bare `dt`
- **BREAKING** `backend-cpp-manual` event handler codegen updated: same `const EventType& <name>` pattern; lifecycle event structs generated from std.core event declarations rather than a hardcoded list
- CLI default backend changes from `cpp-manual` to `cpp-entt`; `cactus game.cactus` now uses cpp-entt unless `--backend cpp-manual` is specified

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `backend-cpp-entt`: Event handler codegen updated — emit `const EventType& <name>` (or alias) parameter in handler functions; remove hardcoded `float dt` injection and `tick.dt → dt` translation
- `backend-cpp-manual`: Event handler codegen updated — emit `const EventType& <name>` (or alias) as handler callback parameter; generate lifecycle event structs from std.core declarations instead of a hardcoded list
- `compiler-cli`: Default backend changes from `cpp-manual` to `cpp-entt`; the "Default backend" scenario and requirement text updated

## Impact

- `src/backends/cpp-entt/system_emitter.cpp`: remove `float dt` parameter, add `const EventType& name`, remove `tick.dt → dt` translation in `rewrite_expr()`
- `src/backends/cpp-manual/event_emitter.cpp` and `src/backends/cpp-manual/system_emitter.cpp`: emit `const EventType& <name>` pattern; generate structs from AST event declarations
- `tests/test_codegen_entt.cpp` and `tests/test_codegen_manual.cpp`: update expected C++ output to use new handler signatures
- `src/main.cpp` default for `--backend` changes from `cpp-manual` to `cpp-entt`
