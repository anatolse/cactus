## Context

`spec/cactus_dsl_spec.md` is the canonical human-readable language specification for Cactus DSL. It defines lexical structure, grammar (EBNF), type system, semantic constraints, and execution model. It is the primary reference for compiler implementation.

The spec was drafted iteratively and now has several internal inconsistencies (two conflicting filter syntaxes), semantic gaps (undefined field access rules, undefined event dispatch model), and aspirational sections (`view`, `interface`, `target: gpu`, `child:`) with no defined semantics. Additionally, `spec/cactus_dsl_spec.md` is not linked into the OpenSpec workflow — it lives in `spec/` outside `openspec/specs/`.

This change is a spec-document revision only. No compiler source code changes are included; those are separate changes that will implement the revised spec.

## Goals / Non-Goals

**Goals:**
- Make `spec/cactus_dsl_spec.md` internally consistent (one filter syntax throughout)
- Define exact field access rules in system handler bodies (Section 6.8)
- Define the event dispatch model — timing, ordering, cascade, targeting (Section 6.9)
- Extend grammar with: `let`/`var` local declarations, `destroy [expr]`, `spawn` as expression, `emit ... to expr`, `filter_entry` with optional `as` alias, `on input()`/`on fixed_tick()`/`on late_tick()` lifecycle names
- Remove sections with no implementable semantics: `view`, `interface`, `target: gpu`, `child:`
- Rewrite all in-spec examples to use the new field access model
- Update `examples/platformer/*.cactus` and `examples/cactus_shop/*.cactus` to be consistent with the revised spec

**Non-Goals:**
- Implementing any of these changes in the compiler (separate changes)
- Defining `std.input`, `std.render`, `std.physics` stdlib modules (covered by `dsl-spec-new-features`)
- Changing `openspec/specs/` (the per-capability specs) — those are separate
- Resolving `enum`/`match` exhaustiveness checking (deferred)
- Adding `input` or `asset` declarations (covered by `dsl-spec-new-features`)

## Decisions

### D1: Filter syntax — block form is canonical, bracket form is removed

**Decision:** The block-indented form is the only valid filter syntax. All bracket-list examples (`filter: [A, B, C]`) in the spec are replaced with the block form. Aliases use inline `as`:

```
filter:
    Position as pos
    Velocity as vel
```

**Rationale:** Section 3.9 already declares the bracket form invalid. The inconsistency was a documentation error. The block form matches all other indented blocks in the language (`apply:`, `config:`, `const:`).

**Alternative considered:** Keep both, make bracket form valid again. Rejected — it adds two parsing paths and contradicts the already-stated intent.

---

### D2: Field access in handlers — mandatory `alias.field` (Path B2 with default-alias)

**Decision:** Inside system handlers, trait fields are accessed exclusively via `alias.field` syntax. If no `as` alias is declared in the filter, the trait name itself is the alias:

```cactus
# "filter: Position" → access as Position.x, Position.y
# "filter: Position as pos" → access as pos.x, pos.y
```

Unqualified bare identifiers inside handlers are always local variables or handler parameters — never trait fields.

**Rationale:** The alternative (unqualified if unique) requires the semantic analyzer to scan all filtered traits for uniqueness at each identifier reference. It also silently breaks when a new trait adds a conflicting field name. Path B2 with default alias gives zero ambiguity with no extra syntax required for simple cases.

**Alternative considered:** Path A (unqualified if unique). Rejected — hard to implement cleanly, creates subtle aliasing bugs as programs grow, harder to give useful error messages.

---

### D3: Local variables — explicit `let`/`var` declarations at statement level

**Decision:** Local variable declarations require `let` (immutable) or `var` (mutable) at statement level. Bare assignment (`x = 5`) always means reassignment of an existing local or an error if the name doesn't exist. Type annotation is optional (inferred from the initializer expression).

```
let_decl = "let" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
var_decl = "var" IDENTIFIER [ ":" type_ref ] "=" expression NEWLINE ;
```

**Rationale:** Consistent with the `let`/`var` distinction already used for trait fields. Python-style implicit creation via assignment was rejected because under Path B2, bare assignment must either reassign a local or be an error — there is no "create trait field" fallback to differentiate against.

---

### D4: `spawn` as expression returning `entity_id`

**Decision:** `spawn` is promoted from statement-only to an expression that returns the created entity's `entity_id`. The statement form is preserved as sugar (discard the return value).

```ebnf
spawn_expr = "spawn" IDENTIFIER "(" [ spawn_arg_list ] ")" ;
primary_expr = ... | spawn_expr ;
```

**Rationale:** Required for entity composition patterns (store child IDs), scene setup (target spawned entities with events), and any code that needs to reference the result of a spawn. Without this, there is no way to get a handle to a newly created entity.

---

### D5: `destroy [expression]` — optional entity_id target

**Decision:** `destroy` accepts an optional expression evaluating to `entity_id`. Without argument: removes the current entity (existing behavior). With argument: removes the entity with that ID.

```ebnf
destroy_stmt = "destroy" [ expression ] NEWLINE ;
```

**Rationale:** Required for explicit lifetime management of entities stored as `entity_id` trait fields. The removal of `child:` means all composition lifetime cleanup must use `destroy entity_id` in `on destroy()` handlers.

---

### D6: `emit ... to entity_id` — targeted event delivery

**Decision:** `emit` gains an optional `to expression` suffix for targeted dispatch. If present, the event is delivered only to the entity matching that `entity_id`. The expression must evaluate to `entity_id`.

```ebnf
emit_stmt = "emit" IDENTIFIER "(" [ arg_list ] ")" [ "to" expression ] NEWLINE ;
```

**Rationale:** Games constantly need to address a specific entity (damage a specific enemy, tell a specific camera to follow a target, etc.). Without targeting, every event is global — systems must manually filter by checking a `target` field convention. Explicit `to` syntax is unambiguous and self-documenting.

---

### D7: User event handlers — implicit `event` object, no parameters

**Decision:** User-defined event handlers declare no parameters. An implicit `event` object is available in scope with fields matching the event's declaration:

```cactus
event PlayerDamaged:
    var amount: int

# in a system:
on PlayerDamaged():
    Health.health -= event.amount   ← event.field access
```

Lifecycle handlers (`tick`, `fixed_tick`, `late_tick`, `input`, `spawn`, `destroy`, `load`, `unload`) retain explicit parameters as before.

**Rationale:** Eliminates the question of whether handler parameter names must match event field names. The `event` object makes the data source unambiguous. The asymmetry between lifecycle and user handlers is natural: lifecycle events are engine-defined with fixed signatures; user events carry a payload that the user defines.

---

### D8: Event dispatch — same-frame cascade, configurable max depth

**Decision:** Events are dispatched same-frame. After each update phase, the event queue is drained up to `max_cascade_depth` levels (default 1). Events emitted at the maximum depth are deferred to the next frame.

`max_cascade_depth` is a per-project configuration (e.g., set in a root module `config:` block or project config file — exact mechanism is a compiler decision, not spec-defined beyond noting it is configurable).

**Rationale:** Same-frame dispatch means `emit PlayerJumped()` in a tick handler causes sound/VFX systems to respond in the same frame — the common expected behavior. A configurable depth cap prevents infinite event cascades while allowing projects that need deeper chains to opt in.

---

### D9: Removal of `view`, `interface`, `target: gpu`, `child:`

**Decision:** All four are removed from the spec. Keywords are removed from the keyword list. Grammar productions are removed.

**Rationale:** All four have no defined semantics. `view` is an underspecified retained-UI concept. `interface` has nothing to implement against (no trait implementation model, no dynamic dispatch). `target: gpu` has no type rules or memory model. `child:` is replaced by explicit `spawn` + `destroy entity_id` + `on destroy()` — which is more transparent and ECS-idiomatic.

---

### D10: `entity_id` — no null value, always valid

**Decision:** `entity_id` always refers to a live entity. There is no null, zero, or "no entity" sentinel. The "optional relationship" pattern is modeled by trait presence/absence (enable/disable). Stale handles (from destroyed entities) are runtime-invalid; the EnTT backend's generation-based IDs handle this transparently.

**Rationale:** A null sentinel would require null-checks before every `emit ... to` and every `destroy entity_id`, adding boilerplate. The ECS-idiomatic solution is richer: if an entity has a `Targeting` trait with `var target: entity_id`, the trait being active means "there is a target" — disable the trait to model "no target."

## Risks / Trade-offs

**[Risk] All existing `.cactus` examples break under Path B2**
→ Mitigation: The examples (`examples/platformer/`, `examples/cactus_shop/`) are rewritten as part of this change. They were aspirational/illustrative anyway (calling undefined functions like `get_input_axis_x()`).

**[Risk] `event` as a reserved identifier in handler scope could conflict with user-named variables**
→ Mitigation: `event` is not a keyword in the language, but the spec defines it as a reserved identifier within user event handler bodies. Users should not name local variables `event` inside event handlers. The semantic analyzer warns if a `let event = ...` shadows it.

**[Risk] Removing `view` and `interface` may surprise users who planned to use them**
→ Mitigation: They are explicitly marked as "deferred — not in v0.1" with a note that they may return in future versions. No implementation ever existed for them, so no code breaks.

**[Risk] Same-frame cascade with depth=1 default may feel wrong for complex event chains (A→B→C)**
→ Mitigation: The depth is configurable. For simple games (the primary audience), depth=1 is correct. The spec clearly documents that events from event handlers at depth=max_cascade_depth wait until the next frame.

**[Risk] `destroy entity_id` is syntactically ambiguous with `destroy` alone**
→ Mitigation: The grammar uses optional expression: `destroy_stmt = "destroy" [expression] NEWLINE`. The parser peeks at the next token; if it's `NEWLINE`, it's the no-arg form. Otherwise it parses an expression. This is unambiguous.

## Open Questions

- **`max_cascade_depth` configuration location**: Where exactly is this set? Options: root `const:` block, a `cactus.toml` project file, or a runtime config trait. The spec marks it "configurable per-project" without specifying the mechanism — left for the compiler implementation to decide.

- **`event` identifier shadowing warning vs error**: If a user writes `let event = 5` inside a user event handler, should it be a compile error or a warning? Leaving as a compiler quality-of-life decision.

- **Ordering of systems across modules**: The spec says "import order across modules" for event handler ordering. When modules are imported in different orders in different root files, this could be non-deterministic. Deferred — the spec notes this as declaration-order within a single compilation unit.
