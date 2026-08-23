## Context

See `proposal.md` - Why for the prior rejection (`archive/2026-08-13-particle-burst-example`) this
change deliberately stays smaller than, and for the sandbox research the language-surface shape is
ported from. This document fixes the concrete grammar, semantics, and codegen shape.

Relevant existing spec facts this design builds on directly:

- `phase_decl` (`spec/cactus_dsl_spec.md` §3.11) already allows arbitrary typed fields with default
  expressions (`phase_field_decl = IDENTIFIER ":" type_ref "=" expression`); no grammar change is
  needed to add a descriptor field to a phase.
- `event_handler`'s trigger is `"on" event_name`, and `event_name = dotted_name` (§3.9) — `on
  particle_pass.vertex:` is already valid grammar today. The only new work is semantic: resolving a
  dotted name where the first segment is a render-pass phase and the second is a recognized stage
  identifier.
- `rule_decl`'s `unary_domain` clauses (`filter:`/`exclude:`/`order by:`) are already all optional
  (§3.8) — a selectionless rule (no `filter:` at all) is already valid syntax, which is exactly the
  shape the fragment handler needs. No grammar change there either.
- The type system has no `vec4`, `mat4`, or `quat`-based 3D projection primitive (§3.14: built-in
  types are `int/float/bool/string`, `vec2/vec3/quat/color`, `entity_id`, asset/input handles,
  `list[T]`). This change targets 2D screen-space rendering only (the same space
  `std.transform.flat.WorldTransform` and the existing `particle-burst` example already use) — it
  does not introduce any 3D projection math, and doesn't need to.
- `handler-execution-graph`'s existing conflict model serializes handlers that "share an observable
  effect domain" (the `effects:` contract clause). This change's synthesized draw step is treated as
  an ordinary participant in the existing `graphics` effect domain — no new domain, no new
  frame/present timing rule.
- `asset_type` (§3.13) is a **closed grammar keyword set** (`"mesh" | "texture" | ...`), not a
  type-identity-based recognition mechanism — worth being explicit that this is *not* an existing
  precedent for how `render.Pass` recognition works. Render-pass recognition here is a genuinely new
  semantic-analysis rule (resolved field type identity, not field spelling), not a reuse of an
  existing pattern. It is a small rule — one new check during phase resolution — but it is new.

## Goals / Non-Goals

**Goals:**
- Let an author write real per-pixel fragment logic for one narrow, useful case (instanced quads —
  covers particles, and later sprites/UI without further language changes) without any `gpu`,
  `shader`, `target`, or `kind` marker anywhere in the source.
- Reuse raylib's existing shader support so this lands as a codegen addition to the current backend,
  not a new backend.
- Fix, in the language-philosophy spec, the constraint that made a prior informal exploration's
  first attempt (author-declared `kind: gpu.*` phases and rules) wrong, so it cannot recur when a
  later change adds real placement *choice* (e.g. compute-shaped rule eligibility).
- Give `examples/particle-burst` the soft-circle look its own design.md already named as the natural
  next step, closing that loop.

**Non-Goals:**
- Any fixed-capacity/dense particle pool or new storage kind — particles remain ordinary
  `spawn`/`destroy` EnTT entities, exactly as the existing example already has them. Combining a
  pool with render passes is a separate, later change if ever pursued.
- General-compute GPU-eligibility inference for *ordinary* rules (`fixed_tick`, `tick`, etc.) — this
  change only affects handlers bound to a render pass's derived vertex/fragment triggers. Deciding
  whether `SimulateParticles`-shaped handlers can run off the CPU path is real, future, harder work
  (it needs an actual second lowering target to choose between, which doesn't exist yet) and is
  explicitly out of scope.
- A CPU software-rasterizer fallback, or any backend-capability negotiation between multiple render
  targets. `Quads` unconditionally lowers to the raylib shader path on `cpp-entt`; a backend without
  shader support simply does not support this construct yet. That is a real, disclosed limitation,
  not a design the author can route around.
- Arbitrary mesh assets, 3D pipelines, author-declared custom varyings, multiple simultaneous
  `Pass` kinds, or reduction/scatter-shaped cross-invocation aggregation. `Quads` is the only `Pass`
  value this change adds.
- Any author-facing device/target/gpu keyword, now or as a design direction — not deferred, ruled
  out (see Decision 4).

## Decisions

### 1. Render-pass recognition: typed descriptor field, not a keyword

```ebnf
(* no grammar change to phase_decl — this is a semantic-analysis addition *)
```

A `phase` is a **render-pass phase** if one of its `phase_field_decl` entries resolves to the
canonical stdlib type `std.render.passes.Pass`. The field's name is not checked — only its resolved
type. A second field resolving to `std.render.passes.Target` names the pass's output target (for
this increment there is exactly one legal value, `render.Target.Screen`; a phase with a `Pass` field
and no `Target` field is diagnosed as incomplete). Both fields must be initialized with a
compile-time-constant expression (an enum-variant literal) — non-constant descriptor expressions are
diagnosed at the field, not at whichever stage handler later depends on them.

```cactus
use std.render.passes as passes

pub phase particle_pass:
    after:
        render
    pipeline: passes.Pass = passes.Pass.Quads
    output: passes.Target = passes.Target.Screen
```

The module alias is deliberately `passes`, not `render` — the stdlib canonical phase `render` already
exists (`pub phase render: after: late_tick; alpha: float = fixed_tick.alpha`, §5.1) and sits in the
same identifier namespace as a `use ... as` alias would; aliasing the new module `render` would
shadow or collide with that phase identifier in any file importing both. This is called out explicitly
so implementers don't hit it as a surprise mid-implementation.

*Alternatives considered:* A `kind:`-style keyword clause on `phase_decl`, mirroring how `asset_type`
is a closed grammar keyword — rejected outright; this is exactly the author-declares-the-device shape
Decision 4 rules out, and (per Context) `asset_type` isn't actually a type-identity precedent to lean
on anyway. A dedicated `render_pass_decl` production distinct from `phase_decl` — rejected; a render
pass is still an ordinary phase (it has `after:`, participates in the same DAG, gets the same
activation/commit semantics) and duplicating the grammar for it would fight the existing model for no
benefit.

### 2. `Quads`: one fixed pipeline shape, not a general shader system

This increment adds exactly one `Pass` value. Its built-in per-invocation fields are fixed, not
author-extensible:

| stage trigger | built-in fields | source |
|---|---|---|
| `<phase>.vertex` (input) | `corner: vec2`, `uv: vec2`, `vertex_index: int` | one of 6 fixed values per instance (2 triangles, matching the reference table in `cactus-particle-gpu-rendering-poc-spec.md` §11.1: corners `(-1,-1)/(1,-1)/(1,1)` and `(-1,-1)/(1,1)/(-1,1)`, UVs `(0,0)/(1,0)/(1,1)`/`(0,0)/(1,1)/(0,1)`) |
| `<phase>.vertex` (output, write-only) | `screen_position: vec2`, `uv_out: vec2`, `tint_out: color` | assigned by the vertex handler body |
| `<phase>.fragment` (input) | `uv: vec2`, `tint: color`, `frag_coord: vec2` | `uv`/`tint` are the vertex stage's `uv_out`/`tint_out`, linearly interpolated across the triangle in screen space; `frag_coord` is the sample's screen position |
| `<phase>.fragment` (output, write-only) | `frag_color: color` | assigned by the fragment handler body; blended onto the target with existing source-over alpha blending |

A phase's own descriptor fields (`pipeline`, `output`) are not visible inside a stage handler body —
only the built-in fields above and, for the vertex handler, whatever traits its `filter:` selected.

*Alternative considered:* author-declared varyings (a `varying:` block on the phase, listing
arbitrary typed fields the vertex handler writes and the fragment handler reads interpolated) —
this is closer to the reference POC spec's general model and is a reasonable **future** increment,
but it adds real surface (a new field kind with different write/read rules than an ordinary phase
field) for zero benefit to the one example this change ships. Fixed fields cover it; deferred until a
second `Pass` kind actually needs different varyings.

### 3. Stage-handler cardinality and body restrictions

- Exactly one handler targeting `<phase>.vertex` and exactly one targeting `<phase>.fragment` must
  exist for a render-pass phase to compile; zero or more-than-one of either is diagnosed by name
  (missing stage / duplicate stage handler, naming both canonical handler identities on the
  duplicate case).
- The vertex handler must be **unary**: `filter:` selects the per-instance entity domain (e.g. every
  `Particle`+`WorldTransform`+`Shape` entity). `exclude:`/`order by:`/`pairs:` are legal on it exactly
  as on an ordinary rule. `where:` is legal (it already only restricts a unary/pair domain per
  §3.8.2, and restricting *which entities* get instanced is meaningful here).
- The fragment handler must be **selectionless**: no `filter:`, `exclude:`, `pairs:`, or `where:`. It
  reads only the built-in interpolated fields (§Decision 2) and constants/`func` calls — it has no
  entity to select, matching the reference POC's stage-9111 rule directly: *"The fragment handler
  MUST be selectionless. It consumes only interpolated stage inputs and constants; it MUST NOT filter
  or access ECS traits."*
- A stage handler body is restricted, checked during semantic analysis (not new grammar): `let`/`var`
  (invocation-local only, never persisted), assignment/compound-assignment to invocation-local
  variables and to the handler's own writable built-in output fields, `if`, and calls to `func` or a
  pure `extern func`. Rejected, each with a located diagnostic naming the statement and the
  restriction: `spawn`, `destroy`, `add`, `remove`, `project`, `emit`, world queries (§3.15.1), and
  any statement that reads a durable trait from the fragment handler (it has none in scope to read).
  The vertex handler *may* read durable traits from its `filter:` bindings (ordinary trait reads,
  unchanged) but may not write them — its only writable state is the built-in output fields.
- Bounded `for` (over an already-materialized `list[T]`) is intentionally **excluded** from the
  allowed set for this increment, unlike the reference POC spec's own allowance — GLSL codegen for a
  Cactus `list[T]` loop is real, unstarted work or with no dependency this backend already has, and
  no example this change ships needs it. Revisit only if a future stage handler genuinely needs one.

### 4. Placement is a backend decision, permanently — even though this change makes no choice yet

Add to `language-philosophy/spec.md`, as a new named requirement under the existing "author/backend
split" material:

> **Device and execution-target placement is always a backend decision, derived from what a
> construct's declared data and used operations require and what the selected backend supports. It
> is never expressed as an author-written marker, keyword, or annotation on a declaration.**

This change has exactly one lowering path for `Quads` (`cpp-entt` → GLSL, unconditionally), so nothing
is technically being *chosen between* yet — there is no `kind:`/`target:` clause to have avoided
adding, because a render-pass phase's grammar never had a slot for one (Decision 1). The requirement
is added now, ahead of there being a real choice to make, specifically so a **future** change that
does introduce a real choice (e.g., inferring whether a compute-shaped `fixed_tick` rule can run
off-CPU once a second lowering target exists) has a binding constraint to build against, rather than
re-deriving it under deadline pressure the way the cited sandbox exploration initially got it wrong
(author-declared `kind: gpu.compute` / a mandatory `gpu.compute rule` prefix / authored
`isolation:`/`reduce:`/`scatter:` clauses — all withdrawn there after review against the reference POC
spec, for the same reason this requirement states directly). This repo has no record of that mistake
happening here; this requirement exists to keep it that way.

*Alternative considered:* Leave this unstated and rely on reviewers to catch a future `kind:`-style
proposal on its own merits — rejected; the sandbox exploration shows a careful, structured
actor/evaluator process still produced the marker-based design on its first pass and needed a second
pass against an external reference to correct it. A named spec requirement is cheaper than repeating
that correction here.

### 5. `examples/particle-burst`: same entities, new render pass

Only the rendering side of `particle_burst.cactus` changes. `Particle`/template/`spawn`/`fixed_tick`
simulation are byte-for-byte unchanged from the archived change's shipped version. The `Shape`
component (`ShapeType.Circle`, flat-filled) is removed from the particle template; a
`WorldTransform`+`Particle`-filtered vertex handler and a selectionless fragment handler replace it:

```cactus
use std.render.passes as passes

pub phase particle_pass:
    after:
        render
    pipeline: passes.Pass = passes.Pass.Quads
    output: passes.Target = passes.Target.Screen

rule ParticleVertex:
    filter:
        tf.WorldTransform as xf
        Particle as particle

    on particle_pass.vertex as v:
        let half_size = PARTICLE_RADIUS
        v.screen_position = vec2(
            xf.position.x + v.corner.x * half_size,
            xf.position.y + v.corner.y * half_size
        )
        v.uv_out = v.uv
        v.tint_out = PARTICLE_COLOR

rule ParticleFragment:
    on particle_pass.fragment as f:
        let dx = f.uv.x - 0.5
        let dy = f.uv.y - 0.5
        let radius_squared = dx * dx + dy * dy

        if radius_squared > 0.25:
            f.frag_color = #00000000
        else:
            let alpha = 1.0 - math.sqrt(radius_squared) * 2.0
            f.frag_color = with_alpha(f.tint, alpha)
```

(`with_alpha` is a small new `pub func` in `std.render.passes`, per `stdlib-render-passes/spec.md`
— expressible purely in Cactus using the `.r`/`.g`/`.b`/`.a` component access and `color(...)`
constructor `add-color-component-access` adds; this change depends on that one landing first, per
`proposal.md` - Impact.) `particle_pass` is declared `after: render` rather than participating as `render`
itself, consistent with `render` staying the canonical, always-present terminal phase; an example
adding its own render pass slots in after it, the same way any other phase extension works today.

### 6. `handler-execution-graph`: one synthetic pass-local edge kind

Add a synthetic node, not authored by any rule, representing rasterization/interpolation between a
render pass's vertex and fragment stage handlers. Edges: vertex-handler node → synthetic
rasterization node → fragment-handler node → pass-completion (feeding the render-pass phase's own
node in the ordinary phase DAG, unchanged). This mirrors the existing model's own precedent for
representing backend-owned, non-authored work as a graph node (the existing `effects:`-domain
conflict edges already reason about handlers without requiring every graph node to be
author-written). No change to activation-commit-boundary or structural-change-timing semantics
(`language-philosophy`'s predictability guarantees, §Context) — a render pass's stage handlers make no
structural changes at all (Decision 3 forbids it), so there is nothing new to reconcile there.

## Risks / Trade-offs

- **GLSL codegen from Cactus expressions is new surface for the backend to get right** (operator
  precedence, `color`/`vec2` component access, `if`/`else` as GLSL control flow) → mitigated by the
  restricted statement/expression subset (Decision 3) being deliberately small — no loops, no calls
  beyond `func`/pure `extern func`, no lists — and by `math.sqrt`/`math.clamp`-style stdlib calls
  needing an explicit "this has a portable GLSL translation" registration per function, not a blanket
  assumption (mirrors how the reference POC spec's §14.4 treats intrinsic portability, applied here
  to exactly the two functions the shipped example needs).
- **A backend without shader support cannot implement this at all** → accepted for this increment
  (Non-Goals); disclosed rather than papered over with a fallback that doesn't exist yet.
- **`examples/particle-burst`'s existing behavior/appearance changes** (flat circles → soft circles)
  → intentional; that example's own design.md already named this as the deferred, expected next step,
  not a regression. `PARTICLE_LIFETIME`/`GRAVITY`/spawn-burst mechanics are unaffected, so its existing
  behavior scenarios in `example-particle-burst/spec.md` (gravity/lifetime/spawn-count) still hold;
  only the rendering-related scenario needs updating (see spec delta).
- **The new language-philosophy requirement binds future work this change doesn't itself do** → by
  design (Decision 4); flagged here so reviewers evaluate it as a standalone commitment, not as
  something this change's own scope proves out end-to-end.

## Migration Plan

Additive for the language and stdlib (new module, new phase-recognition rule, new backend codegen
path) plus one modified example. No existing trait, extern rule, phase, or backend codegen path for
already-shipped constructs changes behavior. Reverting removes the new module, the semantic-analysis
recognition rule, the backend codegen path, and restores `particle-burst`'s prior `Shape`-based
rendering from version control — no data/format migration involved.
