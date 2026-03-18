## 1. Spec Removals

- [x] 1.1 Remove `view` keyword from Section 2.4 keyword list and remove the entire Section 3.13 `view_decl` grammar
- [x] 1.2 Remove `interface` keyword from Section 2.4 keyword list and remove the entire Section 3.14 `interface_decl` grammar
- [x] 1.3 Remove `target` keyword from Section 2.4 keyword list and remove `target_clause` from Section 3.9 `system_decl` grammar
- [x] 1.4 Remove `child` keyword from Section 2.4 keyword list and remove `child_block` and `child_entry` from Section 3.8 `unit_decl` and Section 3.8a `template_decl` grammar
- [x] 1.5 Add a note in the removed sections: "deferred — not in v0.1" with migration guidance

## 2. Grammar — Filter Clause Fixes

- [x] 2.1 Update Section 3.9 `filter_clause` EBNF to introduce `filter_entry = dotted_name ["as" IDENTIFIER] NEWLINE`
- [x] 2.2 Fix all filter examples in Sections 8.4–8.7 that use old bracket syntax `filter: [A, B, C]` — replace with block form
- [x] 2.3 Fix all `filter:` alias examples in Sections 8.6–8.7 that use `filter: [Body as b]` — replace with block form `Body as b` inside indented block
- [x] 2.4 Remove the sentence "The old bracket-list syntax `filter: [A, B, C]` is no longer valid" from Section 3.9 (it's already stated as a note; keep only the EBNF)

## 3. Grammar — Statement Additions

- [x] 3.1 Add `let_decl` and `var_decl` to the `statement` production in Section 3.17 with EBNF: `let_decl = "let" IDENTIFIER [":" type_ref] "=" expression NEWLINE`
- [x] 3.2 Update `destroy_stmt` in Section 3.17 to accept optional expression: `destroy_stmt = "destroy" [expression] NEWLINE`
- [x] 3.3 Add `spawn_expr` to `primary_expr` in Section 3.16: `spawn_expr = "spawn" IDENTIFIER "(" [spawn_arg_list] ")"`; note that spawn returns `entity_id`
- [x] 3.4 Update `emit_stmt` in Section 3.17 to accept optional `to` clause: `emit_stmt = "emit" IDENTIFIER "(" [arg_list] ")" ["to" expression] NEWLINE`
- [x] 3.5 Add `let` and `var` to the Section 2.4 keyword list (they existed as field modifiers; confirm they also serve as statement-level keywords)

## 4. Grammar — Event Handler Updates

- [x] 4.1 Update Section 3.10 `event_handler` — add note that user-defined event handlers have empty parameter lists and use implicit `event` object
- [x] 4.2 Update Section 3.10 `event_name` — add `fixed_tick`, `late_tick`, `input` to the lifecycle event name list
- [x] 4.3 Update Section 3.10 lifecycle handler table — add `on input():`, `on fixed_tick(dt: float):`, `on late_tick(dt: float):` with descriptions
- [x] 4.4 Add `fixed_tick` and `late_tick` to the Section 2.4 keyword list

## 5. Semantic Sections — New

- [x] 5.1 Add Section 6.8 "Field access rules in system handlers" — define resolution order: `alias.field` and `TraitName.field` for trait fields; unqualified = locals or handler params only; `event.field` for user event handlers
- [x] 5.2 Add Section 6.9 "Event dispatch semantics" — define same-frame cascade, configurable `max_cascade_depth` (default 1), FIFO ordering, handler declaration order, events from handlers at max depth deferred to next frame
- [x] 5.3 Update Section 7.2 "System Execution" to show the full four-phase frame model: `on input()` → event phase → `on fixed_tick(dt)` (accumulator) → event phase → `on tick(dt)` → event phase → `on late_tick(dt)` → render

## 6. Type System Update

- [x] 6.1 Update Section 5.1 `entity_id` row — add "always refers to a live entity; no null sentinel; stale handles are runtime-invalid (EnTT generation IDs); absence of relationship modeled via trait presence/absence"
- [x] 6.2 Update the spawn semantics in Section 9.1 — add that `spawn` returns the created entity's `entity_id`
- [x] 6.3 Update `destroy` semantics in Section 3.17 — add the optional `entity_id` argument form with example

## 7. Fix In-Spec Examples

- [x] 7.1 Rewrite the `PatrolSystem` example in Section 3.9 to use `filter: Position as pos` and `pos.pos += ...` style
- [x] 7.2 Rewrite all system examples in Section 9.1–9.4 to use `alias.field` notation and empty params for user event handlers
- [x] 7.3 Rewrite the `Render`, `Movement`, and `Simple` system examples in Sections 8.6–8.7 using block-form filter with aliases
- [x] 7.4 Add `let`/`var` local variable examples to Section 3.17 alongside the statement forms
- [x] 7.5 Add a targeted `emit ... to` example in Section 3.17 and Section 9.1

## 8. Example Files Rewrite

- [x] 8.1 Rewrite `examples/platformer/player.cactus` — update all system bodies to use `alias.field` (e.g., `filter: Position as pos`, `pos.pos += ...`), replace stub input calls with comment placeholders, update event handlers to use `event.` object
- [x] 8.2 Rewrite `examples/platformer/enemies.cactus` — same filter/field-access update
- [x] 8.3 Rewrite `examples/platformer/collectibles.cactus` — same update
- [x] 8.4 Rewrite `examples/platformer/camera.cactus` — same update
- [x] 8.5 Rewrite `examples/platformer/level.cactus` — same update
- [x] 8.6 Rewrite `examples/platformer/ui.cactus` — same update, remove any `view` usage
- [x] 8.7 Rewrite `examples/cactus_shop/player.cactus` — same update
- [x] 8.8 Rewrite `examples/cactus_shop/shop.cactus` — same update
- [x] 8.9 Rewrite `examples/cactus_shop/world.cactus` — same update
- [x] 8.10 Rewrite `examples/cactus_shop/ui.cactus` — remove `view`-based UI patterns
