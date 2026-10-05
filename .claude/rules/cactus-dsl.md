---
paths:
  - "stdlib/**"
  - "examples/**"
---

# Cactus DSL authoring rules

Scope: `.cactus` source in `stdlib/` and `examples/`.

### Avoid duplication

Before adding new Cactus logic, check whether equivalent behavior already exists in
`stdlib/` — scan the relevant module for something close before writing a fresh
implementation.

- If a suitable version already exists, use it — don't re-implement it locally, even
  in a slightly different shape.
- If what exists is almost-but-not-quite right, extract the shared part into the
  nearest shared `stdlib` module and update both call sites, rather than copy-pasting
  and tweaking.
- Never leave two near-identical implementations of the same behavior side by side —
  divergent copies are how one gets the next bug fix and the other doesn't.

### Control-flow nesting

Avoid step/staircase nesting: each `if` stacked inside another `if` forces the reader
to hold one more live runtime condition to understand the branch at the bottom.
Flatten instead of stacking:

- Combine short, independent conditions with `and`/`or` into one guard instead of
  nesting them.
- Prefer early return over nesting the happy path: gate on the negated condition and
  `return` immediately, one gate per line, rather than wrapping the rest of the
  handler body in an `if`.
- Dispatch mutually exclusive cases with value `match` (where the arm form in use
  supports it) instead of an `if`/`else if` chain.
- Treat 3 levels of nested executable control flow as a warning sign and 4 as a
  rewrite trigger. Structural/declarative nesting (`children:` blocks and
  declarations) isn't executable control flow and isn't what this rule targets.

Cactus has no automated nesting lint yet, so this is enforced by hand-review. One
correctness trap when flattening Cactus handlers: `return` exits the *entire* handler
invocation, not a loop iteration — there is no `continue`/`break` in bounded
`for ... in ...:` (see dsl-bounded-foreach). Converting a nested `if` guard inside a
`for` loop into an early `return` changes behavior (it abandons the remaining items)
instead of preserving it; keep the guard nested as an `if` there, or restructure the
loop body, rather than reflexively applying the early-return transform.

### Game-dev simplicity bar

New stdlib/example additions are evaluated against gameplay-core teachability: if it
can't be explained to a beginner in a sentence, it likely belongs in a different
layer. Prefer existing stdlib primitives (math/physics/transform/camera/render/ui)
over hand-rolled logic in game code and showcase examples (teaching examples are the
exception — see `examples/CLAUDE.md`).

### Contacts, zones and solids

- A collider that must block character bodies needs `physics.Solid`; a collider
  without it is a trigger. Bodies, walls and floors get `Solid`; zones, pickups and
  hurtboxes don't.
- To react when something enters or leaves an area (lava, water, a checkpoint, an enemy
  reaching the player), write a pair rule in `group: physics.contacts` that tests
  `physics.touching`, reduces `per:` the entity that needs the transitions, and
  `keep`s a marker trait on it. React with `on added` / `on removed` of that trait.
  Don't re-emit an event every tick from a `where:` match. See the lava and checkpoint
  examples in `spec/cactus_dsl_spec.md` §3.8.6.
- Use `best(binding, by = key)` to name the winning entity (the highest-priority zone)
  instead of hand-rolled max tracking.
