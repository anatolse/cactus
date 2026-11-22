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

### Pass a trait, not its fields

When a `func` needs several fields of one trait or event, take the trait itself as a
parameter (`func to_screen(camera: CameraView, point: vec3)`) and pass the alias
(`to_screen(cv, p)`), instead of unpacking each field into its own argument. The handler's
contract still records only the fields the `func` reads.

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
  `physics.touching` and `keep`s a marker trait on the entity that needs the
  transitions: `keep Caught on player`. A marker needs no `reduce:`; add
  `reduce per <binding>:` only when a kept field or an `if` condition needs an
  aggregate. React with `on added` / `on removed` of that trait.
  Don't re-emit an event every tick from a `where:` match. See the lava and checkpoint
  examples in `spec/cactus_dsl_spec.md` §3.8.6.
- Use `best(binding, by = key)` to name the winning entity (the highest-priority zone)
  instead of hand-rolled max tracking.

### Links between entities

- To link two entities (a tile to a tile, a robot to its task, a player to a room), declare a
  `relation` with `from:`/`to:` ends instead of an entity, or a trait, holding `entity_id`
  fields. A relation can't hold a duplicate pair or a row to a dead entity, and rules read it
  with `R(x, y)` tests in `where:` or a row binding (`r: R`, `each: R as r`). A row binding
  reads its ends' declared traits (`r.from[Tile].x`), so don't copy an end's value into a row
  field.
- When a trait or a state variant points at exactly one entity it reads, such as an attack target
  or a camera's follow target, declare a required link field typed by the target's trait
  (`let target: Unit`). Read it with `order.target[Unit].health` and handle its end with
  `on lost Order.Attack.target:`. Its value must come from a binding, `self`, a named entity or
  another link that carries the trait.
- Keep a relation (`keep R(a, b)` on a `pairs:` rule) when its rows follow from the world, like
  adjacency. Store it (`add R` / `remove R`) when the rows are history or decisions, like claims.
- For "at most one per entity", mark the end `one` and add rows from one phase rule with the
  proven claim shape: `limit: 1 per a, 1 per b` (one part per `one` end), `where: not R(a, _)`
  and `not R(_, b)`, and `add R(a, b)` in its handler. Rank candidates with `order by:` to pick
  the nearest or best first. An event can't claim directly: record it as a trait and claim from
  the phase rule.

### Colors

- Color channels are unbounded floats. Write color math directly (`c * 0.99`); don't clamp in
  between. Drawing clamps once. Blend two colors with `std.color.mix(a, b, t)`, not a
  hand-written `a + (b - a) * t`.
- Fold the colors of several entities with `reduce:`: `sum` for flashes that add, `product` for
  tints and fades that multiply (`product` of floats for scale factors).
- A light's brightness is its color: `color = #FFE0C0FF * 1.5`. Lights have no `intensity`. To change
  brightness at run time, keep the hue in your own trait and write `light.color = hue * strength`.

### Easing

- Shape progress with `std.math.ease` (`ease.out_quad(t)`, or `ease.apply(curve, t)` for a curve kept
  in data) instead of a hand-written curve.

### Values per kind or per place

- For a value per item kind, per board square or per place in a room, declare a trait with
  `dim` lines (`dim item: Item`, `dim x: int in range(0, W)`); its `var` fields hold one value
  per cell. Read and write cells with `e[T][k].f`, `T[k].f` for a `single` trait, or a rule over
  cells (`each: T as c`, a `pairs:` rule with `adjacent(a, b)`).
- Use entities for things that are born, die or receive events. A cell is not an entity: it has
  no lifecycle, events or commands; its carrier does.
- Guard a rule over cells with a `where:` condition that is false at the defaults
  (`c.gas > 0.0`), so it visits only the cells that hold other values.
