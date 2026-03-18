## Why

`spec/cactus_dsl_spec.md` contains internal inconsistencies and semantic gaps that would block a correct implementation:

1. **Filter syntax inconsistency** — Section 3.9 declares the old bracket form `filter: [A, B]` invalid, but Sections 8.4–8.7 use it throughout. The entire spec must use one consistent syntax.

2. **Field access model undefined** — System handler bodies use bare identifiers (`pos`, `health`, `facing`) but the spec never defines what they refer to: trait fields, locals, or something else. This makes name resolution unimplementable.

3. **Local variable declarations missing** — The grammar has `var_assign` (assignment) but no way to *declare* a local variable inside a handler. The examples imply this is possible but provide no syntax for it.

4. **Event dispatch model underspecified** — `emit` is described but dispatch timing, handler ordering, cascade depth, payload binding, and targeted delivery are all undefined.

5. **Aspirational features with no semantics** — `target: gpu`, `view`, and `interface` are present in the grammar but entirely unspecified. They create the impression of a complete spec when the semantics don't exist.

6. **`child:` keyword with no semantics** — The `child:` block exists in unit/template grammar but is never defined. The same functionality is expressible with `spawn` + `destroy entity_id` + explicit `on destroy()` handlers, which is more transparent and consistent with the language's ECS philosophy.

7. **`entity_id` null semantics undefined** — No definition of what an "empty" or "no target" entity_id is. The correct design is that entity_id fields must always hold valid IDs; the "no relationship" state is modeled by trait presence/absence.

8. **`destroy` limited to current entity** — `destroy` only removes the current entity. Removing an entity by ID (e.g., a spawned sub-entity) requires `destroy entity_id`, which the grammar doesn't support.

9. **`spawn` is a statement only** — `spawn` does not return the created entity's ID. For any use case requiring a reference to the spawned entity (targeting, composition, scene setup), this is a hard blocker.

## What Changes

**`spec/cactus_dsl_spec.md`** is revised as follows:

### Removals

- **`target: cpu/gpu` clause** removed from `system_decl`. `target` removed from keyword list.
- **`view_decl` section** removed entirely. `view` removed from keyword list and grammar.
- **`interface_decl` section** removed entirely. `interface` removed from keyword list and grammar.
- **`child_block` / `child_entry`** removed from `unit_decl` and `template_decl`. `child` removed from keyword list.

### Grammar fixes

- **Filter syntax**: all filter and exclude examples throughout the spec (especially Sections 8.4–8.7) updated to use the canonical block form. The `filter_entry` grammar is updated to support optional `as` aliases:
  ```ebnf
  filter_clause = "filter" ":" NEWLINE INDENT { filter_entry } DEDENT ;
  filter_entry  = IDENTIFIER [ "as" IDENTIFIER ] NEWLINE ;
  ```
  If no `as` alias is given, the trait name itself is the access path (e.g., `filter: Position` → fields accessed as `Position.x`).

- **`destroy_stmt`**: extended to accept an optional entity_id expression:
  ```ebnf
  destroy_stmt = "destroy" [ expression ] NEWLINE ;
  ```
  `destroy` alone removes the current entity. `destroy some_id` removes another entity.

- **`spawn_expr`**: `spawn` is now also an expression returning `entity_id`:
  ```ebnf
  primary_expr = ... | spawn_expr ;
  spawn_expr   = "spawn" IDENTIFIER "(" [ spawn_arg_list ] ")" ;
  ```
  The statement form `spawn Foo(...)` is sugar for discarding the return value.

- **`emit_stmt`**: extended with optional `to` targeting:
  ```ebnf
  emit_stmt = "emit" IDENTIFIER "(" [ arg_list ] ")" [ "to" expression ] NEWLINE ;
  ```
  `to expression` evaluates to `entity_id`. Only the matching entity receives the event.

- **Statement-level `let` / `var` declarations** added:
  ```ebnf
  let_decl = "let" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
  var_decl = "var" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
  statement = let_decl | var_decl | var_assign | emit_stmt | spawn_expr NEWLINE
            | destroy_stmt | load_stmt | enable_stmt | disable_stmt
            | return_stmt | expr_stmt | if_stmt ;
  ```

- **`event_handler`**: user-defined event handlers no longer declare parameters. An implicit `event` object is in scope with fields matching the event declaration:
  ```ebnf
  event_handler = "on" event_name "(" [ param_list ] ")" ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;
  ```
  - Lifecycle handlers (`tick`, `fixed_tick`, `late_tick`, `input`, `spawn`, `destroy`, `load`, `unload`) retain explicit parameters.
  - User-defined event handlers use `event.field_name` inside the body.

### New semantic sections

- **Section 6.8 — Field access rules in system handlers**: Defines exactly what identifiers resolve to inside handler bodies:
  - `alias.field` → field of the trait bound to `alias` in the filter clause
  - `TraitName.field` → field of `TraitName` (when no `as` alias given)
  - Unqualified identifier → local variable (declared with `let`/`var`) or handler parameter
  - `event.field` → field of the current event payload (user event handlers only)
  - Unqualified trait names are NOT valid lvalues or rvalues; only `alias.field` or `TraitName.field`

- **Section 6.9 — Event dispatch semantics**: Defines:
  - Events are dispatched same-frame (not deferred to next frame by default)
  - Dispatch is cascade-limited: `max_cascade_depth` (default 1, configurable per-project)
  - At depth 0: tick-phase handlers may emit events → processed at depth 1 (same frame)
  - At depth N (= max_cascade_depth): emits are deferred to the next frame
  - Handler ordering: declaration order of systems within a module; import order across modules
  - Multiple instances of the same event in the queue: processed FIFO
  - `event` implicit object is valid only within user event handler bodies

- **Section 7.2 — Frame execution model**: Updated to show all four tick phases with event processing between them:
  ```
  on input()           → event phase (depth 1 cascade)
  on fixed_tick(dt)    → event phase (depth 1 cascade)  [accumulator-based, 0..N runs]
  on tick(dt)          → event phase (depth 1 cascade)
  on late_tick(dt)     → event phase deferred to next frame
  RENDER               (backend)
  ```

### Type system additions

- **Section 5.1**: `entity_id` updated — always refers to a live entity; no null sentinel exists. The "no relationship" state is modeled by not having the relevant trait active. Stale handles (referencing destroyed entities) are invalid at runtime; the backend (EnTT generation IDs) detects them.

### Updated examples

All system examples in Sections 3.9, 8.4–8.7, 9.1–9.4 are rewritten to use:
- Block-form filter with optional `as` aliases
- `alias.field` or `TraitName.field` notation
- `event.field` in user event handlers
- `let`/`var` for local declarations

## Capabilities

### Modified Capabilities

- `dsl-parser`: grammar changes for `filter_entry`, `destroy_stmt`, `spawn_expr`, `emit_stmt`, `let_decl`, `var_decl`, `event_handler`
- `dsl-semantic-analysis`: new field access rules (6.8), event dispatch semantics (6.9), entity_id validity rules
- `dsl-type-system`: entity_id semantics update

### Removed Capabilities (from spec)

- `view` (deferred — no semantics defined)
- `interface` (deferred — no semantics defined)
- `target: gpu` (deferred — no type rules or memory model defined)
- `child:` hierarchy (replaced by explicit `spawn` + `destroy entity_id` + `on destroy()`)

## Impact

- **`spec/cactus_dsl_spec.md`**: significant revision — all sections affected
- **`examples/platformer/*.cactus`**: rewrite all system bodies to use `alias.field` notation
- **`examples/cactus_shop/*.cactus`**: same
- **Compiler source**: semantic analyzer and parser changes required to implement these rules (separate compiler changes will follow)
- No new files — this is a spec document change only
