## Why

The current event handler syntax is inconsistent: lifecycle events use explicit parameter lists (`on tick(dt: float):`), while user events use empty parentheses plus an implicit `event` object (`on PlayerDamaged():` → `event.amount`). This duality adds cognitive overhead and hardcodes lifecycle event shapes into the compiler rather than the stdlib. Unifying to a single parameter-free syntax with access via the event name (or alias) removes the inconsistency and lets standard events live in `std.core` like any other event declaration.

## What Changes

- **BREAKING** Event handler declarations no longer take a parameter list — the `()` is removed entirely: `on tick:` instead of `on tick(dt: float):`
- **BREAKING** Lifecycle event fields are now accessed via the event name (e.g., `tick.dt`) or an optional alias (`on tick as t:` → `t.dt`), not via directly injected parameters
- **BREAKING** User event fields are now accessed via the event name or alias (`on PlayerDamaged as dmg:` → `dmg.amount`) instead of the implicit `event` object
- Optional `as alias` clause added to all event handlers: `on event_name [as alias]:`
- Standard lifecycle events (`tick`, `fixed_tick`, `late_tick`, `spawn`, `destroy`, `load`, `unload`, `input`) are declared as `event` declarations in `std.core`, giving them proper field definitions and removing the hardcoded parameter validation from the compiler

## Capabilities

### New Capabilities
- `dsl-stdlib-std-events`: Standard event declarations (`tick`, `fixed_tick`, `late_tick`, `spawn`, `destroy`, `load`, `unload`, `input`) defined as `event` blocks in `std.core`, each with their respective fields (e.g., `tick` has `dt: float`)

### Modified Capabilities
- `dsl-parser`: Event handler grammar changes — remove `( param_list )` from `event_handler`, add optional `as IDENTIFIER` alias clause; update EBNF and all lifecycle handler scenarios
- `dsl-semantic-analysis`: Replace lifecycle parameter injection and implicit `event` object with unified event-name (or alias) binding in handler scope; remove hardcoded lifecycle signature validation; validate alias uniqueness against filter aliases

## Impact

- All `.cactus` source files containing event handlers (examples, tests, stdlib) must be updated to the new syntax
- Parser and semantic analyzer require changes; C++ backends are updated in a follow-on change
- Existing spec files for `dsl-parser` and `dsl-semantic-analysis` get delta specs
- Test fixtures and test cases for parser and semantic must be updated
