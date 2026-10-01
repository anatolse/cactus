# Review: `add-render-pass-phases`

Notes from an exploratory review of `openspec/changes/add-render-pass-phases/` (proposal,
design, spec deltas, tasks — status `in-progress`, 0/26 tasks done as of 2026-08-22). Captured
for future development, not a blocking review — the change is well-grounded and internally
consistent; the items below are refinements, not correctness bugs.

## Summary of the mechanism

A `phase` becomes a render pass when one of its fields resolves to the stdlib type
`std.render.passes.Pass` (recognition by resolved type identity, not field name — no grammar
change). A render-pass phase implicitly exposes two derived triggers, `<phase>.vertex` and
`<phase>.fragment`, addressed through the existing `on <dotted-name>:` syntax. This increment adds
exactly one `Pass` value, `Quads`: a fixed, non-extensible instanced-quad topology (6
vertices/instance = 2 triangles) with a fixed built-in field set — no author-declared varyings, no
arbitrary meshes, no texture binding.

```
 ECS entity ──filter:──▶ vertex handler ──▶ [synthetic raster] ──▶ fragment handler ──▶ blend onto Target
 (WorldTransform,           writes:              interpolates          writes:            (source-over,
  Particle, ...)         screen_position,        uv_out→uv,           frag_color          existing alpha
                          uv_out, tint_out        tint_out→tint                             blending)

 one instance = 6 fixed vertices (2 triangles), corners/UVs are BUILT-IN, not authored
```

The `cpp-entt` backend lowers `Quads` to a generated GLSL vertex/fragment pair loaded via
raylib's `LoadShaderFromMemory` (a path that already exists for mesh point-lighting, just never
driven by author-written Cactus before). `examples/particle-burst` is updated to use it for a
soft-circle look, replacing flat `ShapeType.Circle`. A new binding `language-philosophy`
requirement is added: device/execution-target placement is always backend-inferred, never an
author-written marker — this change makes no placement *choice* yet (only one lowering path
exists), but the requirement is added now so a future change that does introduce a real choice
doesn't reach for a `gpu:`/`kind:`-style keyword.

## Two concrete gaps found

**1. `color` has no component access anywhere in the language yet.**
`std.render.passes.with_alpha(base: color, alpha: float) color` needs to read RGB and rewrite
alpha. Checked `dsl-type-system`, `dsl-vector-expressions`, and all of `stdlib/` — `color` is
listed as a primitive type and nothing else; no `.r/.g/.b/.a`, no `color(r,g,b,a)` constructor,
nothing like the `.x/.y` access `vec2` already has. Design.md Decision 5 half-notices this
("`with_alpha`... or `pub func` if expressible purely in Cactus given `color` field access —
implementer's choice") but doesn't resolve it, and `tasks.md` §1.1 doesn't flag that this may
require adding color component access as its own small language feature first, before
`with_alpha` can even be written — which then also needs a GLSL-translation registration, not
just a stdlib one.

**2. Visual correctness of the fragment math has no stated CI verification path.**
`src/backends/cpp-entt/runtime.cpp`'s `load_lighting_shader` shows `CACTUS_RAYLIB_FAKE`
(the headless mode behavioral tests run under) makes `LoadShaderFromMemory` a permanent no-op —
shaders never compile under the fake, by design, since there's no real GL context. Task 7.3
("clicked burst renders as soft-edged circles... mirroring existing gravity/lifetime scenarios")
can verify simulation (spawn count, position, destroy timing) exactly like today, but the actual
fragment-shader radial-falloff math has no stated verification path — headless CI can't see a
pixel. Not a reason to block the change, but the design's Risks section doesn't mention it; worth
tasks.md saying explicitly "visual correctness is manually verified, not CI-gated."

## A "clean triangle" example — and what it reveals

`Quads` has no triangle primitive. Every instance is unconditionally 6 vertices / 2 triangles
(Decision 2's fixed corner table); there's no way to emit 3 vertices, no topology choice at all.
"Render a triangle" has to go through the same trick the shipped particle example uses for
circles: draw the full quad, discard half of it in the fragment stage.

**Floor of the mechanism** (a tinted quad, no tricks — one static `entity`, no `spawn`/`input`
needed, mirroring `examples/blue-square`'s pattern):

```cactus
module hello_triangle

use std.render.passes as passes
use std.transform.flat as tf

pub entity Anchor:
    tf.WorldTransform:
        position = vec2(400.0, 300.0)

pub phase triangle_pass:
    after:
        render
    pipeline: passes.Pass = passes.Pass.Quads
    output: passes.Target = passes.Target.Screen

rule QuadVertex:
    filter:
        tf.WorldTransform as xf

    on triangle_pass.vertex as v:
        let half_size = 100.0
        v.screen_position = vec2(
            xf.position.x + v.corner.x * half_size,
            xf.position.y + v.corner.y * half_size
        )
        v.uv_out = v.uv
        v.tint_out = #33CC99FF

rule QuadFragment:
    on triangle_pass.fragment as f:
        f.frag_color = f.tint
```

**An actual single triangle**, by discarding one half via the fixed corner/UV table from
Decision 2 (the two triangles split along the `uv.x == uv.y` diagonal):

```cactus
rule QuadFragment:
    on triangle_pass.fragment as f:
        if f.uv.x < f.uv.y:
            f.frag_color = #00000000
        else:
            f.frag_color = f.tint
```

This is the closest this mechanism gets to "hello triangle" — a workaround, not a primitive. That
gap is itself the most useful signal for the universality evaluation below.

## Stdlib additions

Exactly one new module, `stdlib/std/render/passes.cactus` (alongside `shapes.cactus`,
`sprites.cactus`, `meshes.cactus`):

```cactus
module std.render.passes

pub enum Pass:
    Quads

pub enum Target:
    Screen

pub func with_alpha(base: color, alpha: float) color:
    # body TBD pending color-component-access resolution (see gap #1 above)
```

Everything else (`corner`, `uv`, `screen_position`, `tint_out`, `frag_color`, etc.) is **not**
stdlib — it's built-in fields materialized by the compiler on the derived `.vertex`/`.fragment`
triggers, invisible to `stdlib/` entirely.

Two things not covered, despite design.md's Goals claiming this mechanism "covers particles, and
later sprites/UI without further language changes":
- **No texture/sampler field anywhere in the `Quads` built-in table.** A textured sprite renderer
  needs to sample a bound texture in the fragment shader; there's no `texture_id`-typed built-in
  input on either stage. Sprites/UI would need a second increment regardless.
- **`math.clamp`/`math.sqrt` are the only registered GLSL intrinsics.** Anything else needs its
  own explicit portability registration (Decision 3, by design) before a stage handler can call
  it — an incremental, ongoing cost, not a one-time one.

## Universality evaluation

| Axis | Assessment | Score |
|---|---|---|
| Geometry | One fixed topology (6-vertex instanced quad). No triangles, lines, fans, or arbitrary meshes as primitives. | 1/10 |
| Pass kinds | One value (`Quads`) exists; the enum is extensible in principle, empty in practice. | 2/10 |
| Instancing model | Always ECS-entity-bound, one instance per filtered entity — no non-entity-driven draws (fullscreen post-fx, UI panels, debug overlays) | 2/10 |
| Varyings | Fixed built-in field set only, no author-declared interpolants | 2/10 |
| Backend coverage | `cpp-entt`/raylib only; explicitly disclosed as a hard limitation, not a fallback gap | 1/10 |
| Statement/expr subset | No loops, no world queries, 3 whitelisted intrinsics | 2/10 |
| **As a general-purpose custom-shading system** | | **~3/10** |
| **As "give particles a soft-shaded look without a new backend"** (its actual, stated, disclosed scope) | Does exactly this, cleanly, with a real precedent (raylib shader loading already exists) and correct language-philosophy discipline | **~8/10** |

The proposal is explicit and repeated about this tradeoff — "Non-Goals" names every low-scoring
axis above as deliberately out of scope, and Decision 2's alternatives-considered section
explicitly declines the more general `varying:`-block design "for zero benefit to the one example
this change ships." The low general-purpose score is the intended shape, not a gap the authors
missed.

### Levers to raise the score, and their cost

| Axis (current) | Lever | Cost / tradeoff | Already anticipated by the design? |
|---|---|---|---|
| Pass kinds (2/10) | Add more `Pass` enum variants (`Triangles`, `Lines`, `Mesh`) | Cheapest lever — the recognition mechanism is already type-based and variant-agnostic. Each new kind still needs its own built-in field table + GLSL codegen path + raylib draw-call shape, so cost scales linearly per kind added. | Yes — the whole point of the enum being extensible; Decision 1 was built for this. |
| Geometry (1/10) | Same lever as above (a `Triangles`/`Mesh` pass kind is what actually buys general geometry) | Same as above. | Yes, same mechanism. |
| Varyings (2/10) | Add the `varying:`-block alternative Decision 2 explicitly considered — author-declared interpolated fields instead of the fixed `screen_position`/`uv_out`/`tint_out` table | Highest-leverage single change for real shader generality — decouples the mechanism from one hardcoded field set. Real new surface: a field kind with write-then-interpolate-then-read semantics unlike any existing phase/trait field, plus generic (not hardcoded) GLSL type codegen. | Yes — and **explicitly rejected for now**: "adds real surface... for zero benefit to the one example this change ships." |
| Texture/sampler binding | Add one built-in `texture: texture_id` field (or a small binding mechanism) to the `Quads` field table | Much smaller than full varyings — closes the sprites/UI gap without a general varying system. | No — not mentioned; this is the one gap under-scoped relative to the proposal's own "later sprites/UI" claim. |
| Instancing model (2/10) | Allow a selectionless vertex handler (draws one fixed fullscreen quad — for post-processing/backgrounds) instead of requiring `filter:` | Breaks the current "vertex handler MUST be unary" invariant (Decision 3); needs a new decision for what "0 instances, 1 draw" means. | No. |
| Statement subset (2/10) | Allow bounded `for` over `list[T]`; keep growing the portable-GLSL intrinsic registry | `for` is real unstarted codegen work. Growing the intrinsic list is cheap and already designed as an open-ended, incremental registry. | Partially — `for` is named and deliberately deferred; the intrinsic registry is designed to grow function-by-function already. |
| Backend coverage (1/10) | Add a second backend with shader support | Not really this proposal's lever — bounded by how many backends the compiler has at all (currently just `cpp-entt`). | No, and out of scope by design. |

**Recommendation:** the cheap, already-designed-for lever is adding `Pass` variants
incrementally as real use cases show up (sprites next would plausibly want a `texture:` binding
specifically, not full varyings yet). The expensive, highest-leverage lever is `varying:`, and
the design's own restraint in *not* pulling it yet looks correct — this is the same discipline
that got the prior GPU-particle mega-proposal rejected
(`openspec/changes/archive/2026-08-13-particle-burst-example`). Building `varying:` speculatively
now would re-commit that mistake with different subsystems.

The one place worth a scope-claim correction, not a design change: either add the `texture:`
field now (small, cheap, makes the "later sprites/UI" claim in design.md's Goals true), or soften
that claim in `proposal.md`/`design.md` so it doesn't promise more than `Quads` actually delivers.
