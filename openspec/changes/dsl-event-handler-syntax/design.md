## Context

Event handlers in Cactus currently have two incompatible forms: lifecycle handlers receive injected C++ parameters (`on tick(dt: float):`), while user-event handlers use empty parens and an implicit `event` accessor (`on PlayerDamaged():` → `event.amount`). The compiler hardcodes lifecycle event shapes (names and parameter types) rather than letting them be declared like any other event. This forces the parser and semantic analyzer to maintain two separate code paths for what is conceptually one construct.

## Goals / Non-Goals

**Goals:**
- Single unified grammar for all event handlers: `on event_name [as alias]:`
- Event data accessed via the event name (or alias) as an implicit read-only local variable in the handler body
- Standard lifecycle events (`tick`, `fixed_tick`, `late_tick`, `spawn`, `destroy`, `input`, `load`, `unload`) declared as `event` blocks in `std.core` — authoritative documentation of their fields
- Remove hardcoded lifecycle parameter validation from the semantic analyzer

**Non-Goals:**
- Changing `emit` statement syntax or semantics
- Changing `event` declaration syntax
- Altering the runtime game loop wiring (backends still treat lifecycle event names specially for hook-up)
- Moving non-lifecycle stdlib events to std.core
- Updating either C++ backend (both `cpp-manual` and `cpp-entt` are deferred to a follow-on change)

## Decisions

### AST node change: remove `params`, add `alias`
`EventHandlerNode` currently has a `params: list<ParamNode>` field. This is replaced with `alias: optional<string>`. The parser no longer collects a parameter list; it optionally collects an alias identifier after an `as` keyword.

*Alternative considered*: Keep `params` as optional and deprecated. Rejected — it would leave dead weight in the AST and require dual handling in the semantic analyzer.

### Standard events declared in std.core; compiler pre-loads them
Lifecycle event types are declared as `pub event` blocks in `std.core` (e.g., `pub event tick:` with field `let dt: float`). The module linker/resolver pre-loads std.core before any user module is analyzed, so lifecycle events are always in scope without requiring an explicit `use std.core`.

User-defined events (`pub event PlayerDamaged:`) continue to work as before — they are declared in user modules and resolved normally.

*Alternative considered*: Keep lifecycle events as compiler builtins and only add the `as alias` syntax. Rejected — this doesn't address the inconsistency or allow stdlib-level documentation.

### Semantic: unified implicit variable binding in handler scope
Upon entering a handler body, the semantic analyzer introduces one implicit local variable:
- Name = the event name (e.g., `tick`), OR the alias if `as alias` is present
- Type = the resolved event type (read-only; fields cannot be assigned)
- Scope = the handler body only

This replaces both the lifecycle parameter injection and the `event` implicit object. The variable follows the same `alias.field` access rules as filter aliases.

*Alternative considered*: Rename the implicit variable uniformly to `event` (e.g., `tick.dt` becomes `event.dt`). Rejected — losing the name information makes handlers harder to read and removes the self-documenting nature of `tick.dt`.

### Codegen deferred to follow-on change
Both C++ backends (`cpp-manual` and `cpp-entt`) are out of scope. Their codegen will be updated after this change lands, once the AST shape (`EventHandlerNode.alias`) is stable. The semantic analyzer's `EventHandlerNode` carries the `alias` field that backends will use to emit `const EventType& <name>` parameters.

## Risks / Trade-offs

- **[Breaking change — all .cactus files]** Every existing event handler must be rewritten from `on tick(dt: float):` to `on tick:` and `dt` references to `tick.dt`. This affects examples, tests, and stdlib files.
  → Mitigation: Update all affected files as part of the implementation tasks (section 5).

- **[std.core always-in-scope assumption]** If a program is analyzed without std.core loaded (e.g., in unit tests that bypass the module linker), lifecycle event types won't resolve.
  → Mitigation: The module linker already auto-includes std.core; tests that need lifecycle events must also load std.core, or the test harness pre-registers event types.

- **[Alias collision with filter aliases]** A handler alias could shadow a filter alias of the same name in the outer system scope.
  → Mitigation: The semantic analyzer reports an error if a handler alias conflicts with an already-bound name in scope.

## Open Questions

- Should `on spawn:` and `on destroy:` bind a variable at all (their event structs have no fields)? Decision: yes — bind the variable for syntactic consistency; it just has no accessible fields. This avoids special-casing in the analyzer.
