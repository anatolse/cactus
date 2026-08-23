## Why

Rendering today is reachable only through opaque `extern rule ... effects: graphics` adapters (`std.render.shapes.ShapeRenderer` and siblings) — pure data traits in, backend-owned C++/raylib draw calls out, nothing about *how* a pixel gets its final color is expressible in Cactus. Spec §1 already permits changing that: *"Rendering... belong to stdlib/backend layers unless explicitly elevated into the language surface by a separate accepted change."* This is that change, for one narrow slice: per-pixel fragment shading of instanced quads.

This is not the first attempt. `openspec/changes/archive/2026-08-13-particle-burst-example` explicitly considered and rejected a much larger version of this idea — fixed-capacity device-resident particle pools, a full compiler-inferred CPU/GPU placement analysis, and vertex/fragment stage triggers, all at once — because it "requires several new compiler subsystems... with no existing precedent in this codebase," and shipped the minimal ordinary-ECS `particle-burst` example instead, on record as "a base the author can extend later... without having invested in throwaway plumbing up front." That example's own design.md names the specific thing it deferred: *"A soft-edged / radially-faded particle look... Revisit only if a future iteration reintroduces custom vertex/fragment rendering."* This proposal is that revisit — sized to avoid the five subsystems the prior attempt correctly flagged as too much at once, not to reintroduce them.

Two things make a genuinely small increment possible where the prior attempt saw only a large one:

1. **The existing backend already has GPU shader support.** `cpp-entt`'s raylib dependency can load and run a custom GLSL shader pair today (`LoadShaderFromMemory` / `BeginShaderMode`) — this proposal only needs to *generate* GLSL text from an authored rule body and call an API raylib already exposes, not build a new backend, a device-eligibility analyzer, or a from-scratch software rasterizer.
2. **A separate internal research exploration (not part of this repo, not binding) already worked through, and then explicitly corrected, the language-surface design.** An informal notation-exploration sandbox first modeled GPU work with author-written `kind: gpu.vertex`/`gpu.compute rule`/`isolation:`/`reduce:`/`scatter:` markers, then — after review against `C:\Users\sevos\Downloads\cactus-particle-gpu-rendering-poc-spec.md`, a POC spec for this compiler — corrected course: *"The language source SHALL describe data, behavior, ordering, and rendering intent. It SHALL NOT prescribe CPU or GPU execution... A rule, trait, or handler MUST NOT contain a `gpu`, `shader`, or `target` marker."* That corrected shape (render-pass phases recognized by a typed compile-time descriptor field, not a keyword; derived `<phase>.vertex`/`<phase>.fragment` triggers; ordinary, unmarked `rule` declarations) is what this proposal ports into the real grammar. Nothing about that sandbox work binds this repo; it is cited here only as prior design work worth not re-deriving.

Scoping to exactly what raylib's shader support unlocks lets this land as one increment instead of the five-subsystem bundle that was rejected before: no fixed-capacity pool (particles stay ordinary `spawn`/`destroy` entities, unchanged from today's example), no general-purpose GPU-eligibility inference for ordinary rules like `fixed_tick` (only render-pass stage handlers are affected), and no new backend (the existing `cpp-entt`/raylib backend gains one bounded codegen path). What *is* established now, permanently, is the constraint that made the sandbox correction necessary: device placement is never something the author writes down.

## What Changes

- Add a `render.Pass` / `render.Target` compile-time descriptor mechanism: a `phase` is recognized as a render pass when one of its fields resolves to the canonical stdlib enum type `std.render.passes.Pass` (field name insignificant — recognition is by resolved type identity). A render-pass phase implicitly exposes two derived triggers, `<phase>.vertex` and `<phase>.fragment`, referenced through the existing `on <dotted-name>:` handler syntax (no grammar change — `event_name` is already `dotted_name`).
- Add exactly one `Pass` value for this increment, `render.Pass.Quads`: a fixed, non-extensible instanced-quad topology (6 vertices/instance, matching a raylib-drawable two-triangle quad) with a fixed built-in field set on each stage trigger — no author-declared varyings, no arbitrary mesh assets, no other pass shapes yet.
- Add stage-handler rules, enforced during semantic analysis (no new statement-level grammar): exactly one vertex-stage and one fragment-stage handler per render-pass phase; the vertex handler is unary (`filter:` selects the per-instance entity domain); the fragment handler is selectionless (no `filter:`/`exclude:`/`pairs:`, no trait access — only built-in interpolated fields and constants); handler bodies are restricted to `let`, assignment to invocation-local variables and to the handler's own stage-output fields, `if`, and calls to `func`/pure `extern func` — `spawn`, `destroy`, `add`, `remove`, `project`, `emit`, and world queries are rejected in a stage handler body, located and diagnosed at the offending statement.
- Extend the `cpp-entt` backend to lower a `Quads` render-pass phase to one generated GLSL vertex/fragment shader pair (translated directly from the two rule bodies) plus a raylib `BeginShaderMode`/instanced-quad draw call sequence, participating in the existing `effects: graphics` scheduling domain the same way `ShapeRenderer` already does — no new backend, no new frame/present timing model.
- Extend `examples/particle-burst` to replace its flat `ShapeType.Circle` rendering with an authored `particle_pass` render pass producing the soft radially-faded look explicitly deferred in that example's own design.md — the simulation (`spawn`/`fixed_tick`/`destroy`, ordinary ECS entities) is unchanged.
- Add `examples/gradient-square`, a new minimal curated example demonstrating vertex-to-fragment color interpolation directly (a single static quad, four fixed per-corner colors, no simulation — see the Example section below) — registered as a regular CMake example build target and a curated `cpp-entt` compilation-coverage case, the same way `particle-burst` already is.
- Add a named, binding language-philosophy requirement: device/execution-target placement is always a backend decision derived from what a construct's data and operations require, never an author-written marker. This change has only one lowering path for `Quads` today, so nothing is actually chosen between yet — the requirement exists so that when a future change adds a second placement option (e.g., inferred eligibility for ordinary compute-shaped rules), no `gpu`/`target`/`kind`-style keyword gets introduced to select it.

## Capabilities

### New Capabilities
- `dsl-render-passes`: the core language mechanism — render-pass phase recognition by descriptor field type, derived vertex/fragment stage triggers, stage-handler cardinality and body restrictions.
- `stdlib-render-passes`: the `std.render.passes` module — `Pass`/`Target` enums and the `Quads` pass kind's fixed built-in field tables.
- `example-gradient-square`: a new minimal curated example — a single static quad with a distinct authored color per corner, demonstrating vertex-to-fragment color interpolation directly.

### Modified Capabilities
- `handler-execution-graph`: add a synthetic, non-authored pass-local edge connecting a render pass's vertex-stage node to its fragment-stage node, and the pass node to downstream phases — mirrors the existing contract-conflict-edge model, adds nothing authored.
- `backend-cpp-entt`: add GLSL codegen for `Quads`-kind render passes and the raylib shader/instanced-draw call sequence.
- `example-particle-burst`: swap the flat-circle look for the new render pass; ECS/simulation shape unchanged.
- `language-philosophy`: add the "placement is backend-inferred, never authored" requirement, citing the render-pass mechanism as its first instance.
- `cmake-example-build-targets`: register `gradient-square` as a regular CMake example build target, mirroring `particle-burst`'s existing registration.
- `example-cpp-compilation-tests`: add `gradient-square` as a named curated `cpp-entt` compilation case, mirroring `particle-burst`'s existing case.

## Example: vertex-to-vertex color gradient

The `Quads` field table (design.md Decision 2) writes `tint_out` once per vertex invocation and
hands the fragment stage a linearly-interpolated `tint` — vertex-to-vertex color blending falls
out of the mechanism for free. `examples/particle-burst` (below) never actually exercises that:
every corner of a given particle gets the same flat `PARTICLE_COLOR`, so its `tint_out` is
uniform per instance and there is nothing to interpolate. The smallest program that demonstrates
the feature's namesake behavior is a single static quad with a different color per corner:

```cactus
module gradient_square

use std.transform.flat as tf
use std.render.passes as passes

const:
    SQUARE_SIZE = 200.0

pub phase gradient_pass:
    after:
        render
    pipeline: passes.Pass = passes.Pass.Quads
    output: passes.Target = passes.Target.Screen

pub entity GradientSquare:
    tf.WorldTransform:
        position = vec2(400.0, 300.0)

rule SquareVertex:
    filter:
        tf.WorldTransform as xf

    on gradient_pass.vertex as v:
        let half = SQUARE_SIZE * 0.5
        v.screen_position = vec2(
            xf.position.x + v.corner.x * half,
            xf.position.y + v.corner.y * half
        )
        v.uv_out = v.uv
        if v.corner.x < 0.0:
            if v.corner.y < 0.0:
                v.tint_out = #FF0000FF
            else:
                v.tint_out = #FFFF00FF
        else:
            if v.corner.y < 0.0:
                v.tint_out = #00FF00FF
            else:
                v.tint_out = #0000FFFF

rule SquareFragment:
    on gradient_pass.fragment as f:
        f.frag_color = f.tint
```

`SquareVertex` runs once per corner of the one selected instance (six invocations — two
triangles sharing the `(-1,-1)`/`(1,1)` diagonal, per Decision 2's fixed corner table) and picks
one of four fixed colors by corner sign, exactly the branching shape `ParticleFragment` already
uses (`if`/`else`, no `elif` — the grammar has none). `SquareFragment` does no falloff math at
all; it just passes the interpolated `tint` straight through, so the entire visible gradient is
produced by the backend's rasterizer interpolation, not by any authored per-pixel formula.

This ships as a real curated example, `examples/gradient-square/gradient_square.cactus` — a
regular CMake example build target and a curated `cpp-entt` compilation-coverage case, registered
the same way `particle-burst` already is (see `example-gradient-square`,
`cmake-example-build-targets`, and `example-cpp-compilation-tests` in Capabilities and Impact
below).

## Impact

- **Dependency landed: `add-color-component-access`.** Archived 2026-08-22
  (`openspec/changes/archive/2026-08-22-add-color-component-access`) and merged into
  `openspec/specs/dsl-vector-expressions/spec.md` (`.r`/`.g`/`.b`/`.a` access, a `color(...)`
  constructor, and `color` operator-matrix rows, per `b42fe8a`). `stdlib-render-passes/spec.md`'s
  `with_alpha` requirement commits to "a pure Cactus function (not `extern`)... its body
  is GLSL-translatable by the same restricted-statement rule stage handlers use" — that body
  needs to preserve RGB channels and rewrite alpha by reading and reconstructing a `color`, which
  the landed change now provides syntax for. This removes the "`pub extern func`/`func`...
  implementer's choice" hedge in `design.md` Decision 5 — `with_alpha` is a pure `func`, as the
  spec already requires, not an open implementation choice. Nothing in this change is still
  blocked by it; implementation of `add-render-pass-phases` itself has not started (all 26 tasks
  in `tasks.md` remain unchecked).
- New stdlib module `stdlib/std/render/passes.cactus` (or equivalent — exact file per existing stdlib module layout convention).
- Frontend/semantic-analysis changes: descriptor-field-type recognition for render-pass phases, derived-trigger resolution for `on <phase>.vertex:`/`on <phase>.fragment:`, stage-handler cardinality and body-restriction validation, new diagnostics for each violation.
- `handler-execution-graph` construction: one new synthetic node/edge kind, pass-local only.
- `src/backends/cpp-entt`: new codegen path (Cactus stage-handler body → GLSL, plus raylib shader setup/draw call emission) for the `Quads` pass kind only.
- `examples/particle-burst/particle_burst.cactus`: modified (rendering only; no change to `CMakeLists.txt` or `tests/test_example_cpp_compilation.cpp` registration, since the example is already registered and no new build artifacts beyond generated C++ are introduced).
- `examples/gradient-square/gradient_square.cactus`: new file (per the Example section above).
- `CMakeLists.txt`: new `cactus_add_example(example_gradient_square_generated ...)` registration, mirroring the existing `example_particle_burst_generated` block (`CMakeLists.txt:483-484`).
- `tests/CMakeLists.txt`: new `CACTUS_EXAMPLE_GRADIENT_SQUARE_SOURCE`/`_TARGET` compile definitions, mirroring the particle-burst entries (`tests/CMakeLists.txt:287,289`).
- `tests/test_example_cpp_compilation.cpp`: new curated case entry named `gradient-square`, mirroring the existing `particle-burst` case (`tests/test_example_cpp_compilation.cpp:225`).
- `openspec/specs/cmake-example-build-targets/spec.md` / `openspec/specs/example-cpp-compilation-tests/spec.md`: one new requirement each, registering and curating `gradient-square`.
- `openspec/specs/language-philosophy/spec.md`: one new requirement.
- No change to `dsl-pair-relations`, `dsl-update-phases`' existing `phase`/`from`/`after`/`every`/`max` grammar, `handler-contracts`' `reads`/`writes`/`effects` clause grammar, or any existing rendering trait (`std.render.shapes`, `std.transform.flat`) — all additive.
