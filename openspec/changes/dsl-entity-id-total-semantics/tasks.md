## 1. Semantic Analyzer — `entity_id` error message update

- [ ] 1.1 In `src/frontend/semantic_analyzer.cpp`, find the `entity_id == 0` comparison check and update the error message from "entity_id has no null value; use trait enable/disable to model absent relationships" to "entity_id has no null literal; use `exists(id)` to test handle validity or `add`/`remove` to model absent relationships via trait presence"
- [ ] 1.2 Update `tests/test_semantic.cpp` to match the new error message text

## 2. Semantic Analyzer — `exists()` built-in

- [ ] 2.1 Add `exists` as a recognized built-in call in `src/frontend/semantic_analyzer.cpp`: when a `CallExpr` with callee `IdentExpr("exists")` and one argument is encountered, validate argument type is `entity_id`, validate context is system handler body, resolve result type as `bool`
- [ ] 2.2 Report error "`exists()` requires world access; only allowed inside system event handlers" when used in `func` body
- [ ] 2.3 Report error "`exists()` argument must be of type `entity_id`" when argument type is not `entity_id`
- [ ] 2.4 Update `tests/test_semantic.cpp`: add tests for valid `exists()`, non-entity_id argument error, func-body context error

## 3. EnTT Backend — Cross-entity operation validity guards

- [ ] 3.1 In `src/backends/cpp-entt/system_emitter.cpp`, for `AddTraitStmt` with a `target_expr` (cross-entity case), wrap generated `emplace_or_replace<T>(target)` with `if (registry.valid(target)) { ... }`
- [ ] 3.2 For `RemoveTraitStmt` with a `target_expr`, wrap `registry.remove<T>(target)` with `if (registry.valid(target)) { ... }`
- [ ] 3.3 For `DestroyStmt` with an explicit entity expression (not self), wrap `registry.destroy(id)` with `if (registry.valid(id)) { ... }`
- [ ] 3.4 Self operations (`add Trait` / `remove Trait` / `destroy` with no target) do NOT get validity guards — the current entity is always valid within its own handler

## 4. EnTT Backend — Targeted event dispatch validity guard

- [ ] 4.1 In `src/backends/cpp-entt/event_emitter.cpp`, for `EmitStmt` with a `to` expression, wrap the event dispatch call with `if (registry.valid(target_entity)) { ... }`
- [ ] 4.2 Update `tests/test_codegen_entt.cpp`: add test for targeted emit with validity guard in generated output

## 5. EnTT Backend — `exists()` expression codegen

- [ ] 5.1 In `src/backends/cpp-entt/system_emitter.cpp` (or expression emitter), generate `registry.valid(arg_value)` for `CallExpr` with callee `exists` and one `entity_id` argument
- [ ] 5.2 Update `tests/test_codegen_entt.cpp`: add test verifying `exists(f.target)` compiles to `registry.valid(f_target)`

## 6. EnTT Backend — `match entity_id:` validity guard

- [ ] 6.1 In the `TraitMatchStmt` codegen, wrap the entire `if/else-if` chain with an outer `if (registry.valid(subject)) { ... }` check
- [ ] 6.2 Update `tests/test_codegen_entt.cpp`: add test verifying that generated trait match code starts with `if (registry.valid(...))`

## 7. Spec documentation cross-references

- [ ] 7.1 In `openspec/changes/dsl-dynamic-traits/specs/dsl-dynamic-traits/spec.md`, add a note to the cross-entity targeting requirement: "Operations on stale handles are silent no-ops per dsl-entity-id-total-semantics"
- [ ] 7.2 In `openspec/changes/dsl-trait-pattern-match/specs/dsl-trait-pattern-match/spec.md`, add a note to the match statement requirement: "When the subject handle is stale, no arm fires — see dsl-entity-id-total-semantics"
