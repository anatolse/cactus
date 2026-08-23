## 1. Stdlib: `std.render.passes`

- [x] 1.1 Add `stdlib/std/render/passes.cactus` (path per existing stdlib module layout
      convention): `pub enum Pass: Quads`, `pub enum Target: Screen`, `pub func with_alpha(base:
      color, alpha: float) color`
- [x] 1.2 Register the new module for import resolution the same way sibling `std.render.*`
      modules are registered (module resolution here is purely filesystem-driven — placing the
      file at the conventional path is the entire registration step; confirmed via
      `module_resolver.cpp`)

## 2. Frontend: render-pass phase recognition and derived triggers

- [x] 2.1 During phase resolution, recognize a `phase` as a render-pass phase when a field
      resolves to canonical type `std.render.passes.Pass`; require exactly one
      `std.render.passes.Target`-typed field; diagnose missing `Target` and diagnose non-constant
      descriptor expressions at the field
- [x] 2.2 Resolve `<phase>.vertex` / `<phase>.fragment` as derived trigger identities for a
      recognized render-pass phase; diagnose the stage suffix on a non-render phase
- [x] 2.3 Validate stage-handler cardinality: exactly one vertex-stage and one fragment-stage
      handler per render-pass phase; diagnose missing/duplicate cases by canonical handler
      identity
- [x] 2.4 Validate vertex handler is unary (`filter:` required, `pairs:` rejected) and fragment
      handler is selectionless (`filter:`/`exclude:`/`pairs:`/`where:` all rejected, no durable
      trait reads)
- [x] 2.5 Validate the restricted stage-handler statement/expression subset (`dsl-render-passes`,
      "Stage handler body is restricted..."); reject `spawn`/`destroy`/`add`/`remove`/`project`/
      `emit`/world queries/bounded `for`, each with a located diagnostic
- [x] 2.6 Validate stage-handler writes: vertex handler may only assign to its own built-in
      output fields (`screen_position`/`uv_out`/`tint_out` for `Quads`), never to a filtered
      trait; fragment handler may only assign `frag_color`
- [x] 2.7 Validate that every function called from a stage handler body is either a `func` or an
      `extern func` on the backend's registered-portable-GLSL-translation list (§4); diagnose
      unregistered calls. Implemented as `HandlerTriggerKind::RenderStage` (new trigger kind,
      distinct from `Phase`/`Event`), plus new `SemanticAnalyzer` passes
      (`recognize_render_pass_phases`, `validate_render_pass_descriptor_fields`,
      `validate_render_pass_stage_handlers` + helpers) in `semantic_analyzer.{hpp,cpp}`. Shared
      built-in field tables (`src/common/render_pass_builtin_fields.hpp`) and the portable-GLSL
      intrinsic registry (`src/common/render_pass_intrinsics.hpp`) are single-source-of-truth
      headers consumed by both this validation and the future backend codegen (§4). 15 new Catch2
      cases in `tests/test_semantic_modules.cpp` (`[render-passes]` tag); full existing suite
      (semantic, semantic_modules, parser, codegen_entt, multi_module_integration, program_linker,
      module_artifact) still green.

## 3. `handler-execution-graph`: pass-local synthetic edges

- [x] 3.1 Add a synthetic rasterization node per render-pass phase; wire vertex-handler-node →
      synthetic node → fragment-handler-node. Implemented as a new `RenderPassPlan` record
      (`{phase, vertex_handler, fragment_handler}`) in `ExecutionGraph::render_passes` — a
      dedicated record rather than a generic graph node, since the vertex→fragment relationship is
      a fixed backend-mediated data flow, never a scheduler conflict; confirmed by test no direct
      `schedule_edges` entry connects the two handlers. Also fixed a real gap found while wiring
      this up: `ProgramLinker::merge_into` and `ModuleArtifact` save/load didn't carry the new
      field, so it would have silently vanished for every real (multi-module) compile — fixed both,
      bumped `ModuleArtifact::CURRENT_VERSION` 11→12, added a round-trip test.
- [x] 3.2 Wire the render-pass phase's own node into the ordinary phase DAG (`after:` edges)
      unaffected by the internal synthetic edges; confirm no structural-commit interaction is
      introduced (stage handlers can't issue structural commands per §2.5). No new mechanism
      needed: a render-pass phase is an ordinary `PhasePlan` (unaffected by `render_passes`), and
      §2.5's body restriction already rejects every structural-command statement kind in a stage
      handler body, so there is nothing for a structural-commit edge to represent.

## 4. Backend (`cpp-entt`): GLSL codegen and raylib draw

- [x] 4.1 Implement Cactus-expression-to-GLSL translation for the restricted stage-handler subset
      (§2.5): literals, `vec2`/`color` construction and component access, arithmetic/comparison/
      boolean operators, `if`/`else`, local `let`/`var`, assignment to built-in output fields.
      New `src/backends/cpp-entt/render_pass_emitter.{hpp,cpp}`: a recursive expression/statement
      translator threading a small `GlslType{Float,Bool,Vec2,Color}` inference alongside text
      generation (mirrors `infer_phase_expr`'s type-while-emitting shape). Vertex `screen_position`
      reaches NDC via raylib's own automatically-populated `mvp` uniform (no manual projection
      math); `corner`/`uv`/`vertex_index` come from `gl_VertexID` against a compile-time constant
      table, not real vertex attributes, so the draw call needs no custom VBO.
- [x] 4.2 Register portable GLSL translations for `std.math.sqrt`, `std.math.clamp`, and
      `std.render.passes.with_alpha`. `sqrt`/`clamp` share GLSL's own built-in names 1:1
      (`src/common/render_pass_intrinsics.hpp`, consumed by both this and §2.7's validation).
      `with_alpha` is a plain (non-extern) `func`, per spec not a registered intrinsic — its body is
      inlined at each call site instead (parameter substitution, not a real GLSL function).
- [x] 4.3 Generate one GLSL vertex shader and one GLSL fragment shader per `Quads` render-pass
      phase from its stage handler bodies; embed as string literals in generated C++ (no new
      asset files, no CMake changes). Confirmed by hand-inspecting generated output for both
      shipped examples — correct translation including const-inlining, hex-color-to-vec4, nested
      if/else, and `with_alpha` inlining.
- [x] 4.4 Emit raylib `LoadShaderFromMemory` setup and a per-activation `BeginShaderMode` +
      instanced-quad draw call, gated on the render-pass phase's activation, participating in the
      existing `effects: graphics` conflict-ordering machinery. New generic runtime helpers
      `ensure_render_pass_shader`/`draw_render_pass_quad_instance` in `runtime.{hpp,cpp}` (mirrors
      the existing lighting-shader ensure/nullptr pattern); per-entity view loop + uniform upload is
      generated (program-specific), draw uses `rlBegin(RL_TRIANGLES)`/6×`rlVertex2f`/`rlEnd` since
      the shader ignores submitted vertex positions entirely (derives them from `gl_VertexID`).
      Wired into `emit_graph_handler_dispatch` (`cpp_entt_codegen.cpp`): a render-pass phase's
      `generated_dispatch_phase_<name>` body is substituted wholesale instead of the generic
      per-handler loop (which finds nothing for `RenderStage`-kind triggers by design). Also
      exempted `RenderStage` handlers from `system_emitter.cpp`'s generic per-rule C++ function
      emission (they have no C++ trigger type; their bodies go to GLSL, not C++).
- [x] 4.5 Confirm the draw step's relative order against existing `std.render.shapes`/
      `std.render.sprites`/`std.render.meshes` renderers follows the same deterministic
      stable-declaration-order tie-break already specified for `effects: graphics` conflicts. No
      new ordering mechanism needed: a render-pass phase is an ordinary `after:`-chained phase, so
      its relative order against phase-bound renderers is already fully determined by the existing
      phase DAG + `stable_topological_order`'s declaration-order tie-break — confirmed by reading
      `execution_graph_scheduler.cpp`'s per-activation leveling, which applies uniformly regardless
      of trigger kind.

  **Bugs found and fixed while implementing this section** (both pre-existing, unrelated to
  render passes on their own terms, but surfaced because this change is the first thing to
  exercise the affected paths):
  1. `ResolvedPhase` lookups by bare local name silently failed after `ProgramLinker::merge_into`,
     which re-keys `DecoratedProgram::phases` by `canonical_id` once linking runs — added a
     symbol-identity-based `find_resolved_phase` helper in `cpp_entt_codegen.cpp` (mirrors
     `EnttCodegenUtils::find_trait`'s existing simple-name/canonical-id duality handling, which
     trait/struct/enum lookups already had but phase lookups didn't).
  2. `add-color-component-access`'s `.r`/`.g`/`.b`/`.a` byte-to-normalized-float fix only covered
     two of the three places component access can appear in generated C++: `rewrite_expr`'s
     pair-bound-chain branch (system_emitter.cpp) and `EnttCodegenUtils::emit_expr`'s two overloads
     (type_utils.cpp, fixed here for real programs but the actual bug is in shared code). The third
     — a plain local/parameter value's component access (e.g. `with_alpha`'s own `base: color`
     parameter) — had no fix at all, so `std.render.passes.with_alpha`'s real C++ body (emitted
     into every program that imports the module, called or not) read raw, unnormalized bytes,
     failing clang-tidy's `bugprone-narrowing-conversions` on any curated example importing
     `std.render.passes`. Added `NumericKind::Color` (system_emitter.cpp) so `rewrite_expr`'s
     non-pair-scope `MemberExpr` case can recognize a color-typed local/parameter the same way it
     already recognizes `Int`/`Float` ones, and apply the same normalized read.
  3. **Found via real (non-headless) manual testing, not automated coverage** — a structural gap
     worth naming: `CACTUS_RAYLIB_FAKE` makes `LoadShaderFromMemory` a permanent no-op, so no
     automated test in this repo ever actually compiles the embedded GLSL text with a real GLSL
     compiler; every test up to this point only validated the *C++* side (compiles, links,
     clang-tidy clean). Running `example_gradient_square_generated` with a real GL context
     surfaced a real GLSL compile error: `half` (`gradient_square.cactus`'s own `let half = ...`)
     is a reserved word in GLSL, though an entirely ordinary identifier in Cactus. Fixed at the
     root rather than by renaming the one local: every `let`-bound stage-handler local is now
     unconditionally emitted under a `let_`-prefixed name (`render_pass_emitter.cpp`'s new
     `local_reference()`/`GlslCtx::let_names`), so no future author-chosen identifier can collide
     with any current or future GLSL reserved word. Locked in with a new codegen-text regression
     test (`test_codegen_entt.cpp`) asserting the mangled name appears and the raw one doesn't —
     this can't replace an actual GLSL compile check (no such tool is vendored in this repo), so
     the underlying test-coverage gap for GLSL *syntax* errors (as opposed to visual/pixel
     correctness, already disclosed at §7.3/§8.6) remains and should be named as a known limitation
     if this mechanism is extended further.
  4. Separately, `emit_render_pass_dispatch_body`'s phase-symbol fallback used
     `phase.symbol_id.value_or(make_symbol_id(...))` — `value_or`'s argument is evaluated eagerly
     regardless of whether the optional holds a value, so constructing that fallback `SymbolId`
     with an empty module name (a real state in at least one test harness) threw even though the
     result was always discarded. Replaced with an explicit presence check; a render-pass phase's
     `symbol_id` is always set by the time this runs, so the fallback was dead weight anyway.
  5. **Found via real (non-headless) manual testing**: after fixing bug 3 (GLSL syntax), both
     examples still rendered as a blank window with no error of any kind — shaders compiled and
     linked cleanly, dispatch ran every frame, but nothing appeared on screen. Root-caused with a
     throwaway screenshot-diffing debug harness (a temporary CMake target including a render-pass
     example's generated `.cpp` directly, running it against a real GL context for a fixed frame
     count, then `TakeScreenshot` — not kept in the tree). Bisected through several false leads
     (uniform upload timing, near-plane Z precision, raylib's `mvp` auto-wiring) down to the actual
     cause: `draw_render_pass_quad_instance()` (`runtime.cpp`) submitted its 6 vertices via
     `rlBegin(RL_TRIANGLES)`/`rlVertex2f`/`rlEnd()`, and on this environment's GPU/driver
     (Intel UHD 620), an `RL_TRIANGLES` immediate-mode batch whose vertices end up at a
     large/non-trivial on-screen position after the vertex shader's transform is silently dropped —
     reproduced even with raylib's own default shader and no custom GLSL involved at all. The
     identical geometry submitted via `rlBegin(RL_QUADS)` (4 vertices, no change to the shader)
     rendered correctly. Fixed by switching `draw_render_pass_quad_instance()` from
     `RL_TRIANGLES`/6 vertices to `RL_QUADS`/4 vertices — also raylib's own preferred mode for this
     kind of untextured quad (`DrawRectangleRec`/`DrawTriangle` use `RL_QUADS` internally via
     `SUPPORT_QUADS_DRAW_MODE`). Re-verified via the same screenshot harness: `gradient-square` now
     shows the four-corner gradient and a directly-spawned `particle-burst` entity renders as the
     expected small soft-edged dot. This is a second, distinct instance of the same underlying gap
     named in bug 3 — no automated test in this repo drives a real GL context, so a change that
     compiles and links cleanly can still render nothing, and only manual/screenshot verification
     against real hardware catches it.

## 5. `language-philosophy`

- [x] 5.1 Add the "Device and execution-target placement is a backend decision, never authored"
      requirement under the existing author/backend-split material. Also removed a pre-existing
      dead `target: cpu`/`target: gpu` rule clause (parsed but never consumed by semantic analysis
      or codegen) that directly contradicted this new requirement — grammar (`parser.cpp`/
      `parser.hpp`), AST (`ast.hpp`'s `RuleNode`/`ExternRuleNode`), and the 4 parser tests
      exercising it were all removed; confirmed unused in `spec/`, `stdlib/`, and `examples/`

## 6. `examples/particle-burst`

- [x] 6.1 Remove `std.render.shapes.Shape` from `ParticleTemplate`; add imports for
      `std.render.passes` and `std.math` (`std.math` was already imported; added `std.render.passes
      as passes`, removed `use std.render.shapes`)
- [x] 6.2 Add `particle_pass` (`after: render`, `pipeline: passes.Pass = passes.Pass.Quads`,
      `output: passes.Target = passes.Target.Screen`)
- [x] 6.3 Add `ParticleVertex` (filter `WorldTransform`+`Particle`; write `screen_position`,
      `uv_out`, `tint_out`) and `ParticleFragment` (selectionless; write `frag_color` via the
      radial-falloff formula and `with_alpha`) per `design.md` Decision 5. Added `PARTICLE_RADIUS`
      (5.0, matching the old `Shape.size`'s 10.0-diameter circle) and `PARTICLE_COLOR` (`#FF6B6BFF`,
      the old `Shape.color`) consts; `with_alpha` called qualified (`passes.with_alpha(...)`) since
      this language requires qualified references to imported symbols (design.md's own inline
      example elides the qualifier, which would not resolve against the actual import rules).
- [x] 6.4 Confirm simulation (`spawn`/`fixed_tick` gravity+lifetime/`destroy`) is byte-for-byte
      unchanged from the currently-shipped example — diffed: only the template's trait list and the
      rendering-related rules changed; `EmitParticleBurst`/`SimulateParticles` bodies untouched.
- [x] 6.5 Update the example's rendered appearance in any committed reference screenshot/doc, if
      one exists, to reflect the soft-circle look — none exists in the repo; nothing to update.

## 7. Verification

- [x] 7.1 `examples/particle-burst/particle_burst.cactus` compiles with `--backend cpp-entt` and
      produces valid C++ output including the embedded GLSL pair. Verified both via direct
      `cactus.exe` invocation and via the real `example_particle_burst_generated` CMake target,
      which builds and links successfully.
- [x] 7.2 Diagnostic tests: missing/duplicate stage handler, non-unary vertex handler, filtered
      fragment handler, forbidden statement in a stage handler body (each per §2), unregistered
      GLSL-translation call. Covered by the 15 Catch2 cases added in §2
      (`tests/test_semantic_modules.cpp`, `[render-passes]` tag) plus the graph-structure test
      added in §3 — one case per scenario listed here.
- [x] 7.3 Runtime/behavioral test: a clicked burst renders as soft-edged circles at the cursor
      position and fades out as particles age/die, mirroring the existing example's gravity/
      lifetime scenarios (`example-particle-burst/spec.md`, unmodified requirements). New
      `tests/test_particle_burst_headless_behavior.cpp` (3 cases): a click spawns exactly
      `PARTICLE_COUNT` particles near the cursor at roughly `PARTICLE_SPEED`; no click spawns
      nothing; gravity measurably increases downward velocity and every particle is destroyed
      within 5 simulated seconds (well past `PARTICLE_LIFETIME`). Only ECS/simulation state is
      asserted — fragment-shader pixel output has no path under `CACTUS_RAYLIB_FAKE` (§8.6's
      disclosed limitation applies equally here).
- [x] 7.4 Confirm no change to generated output for programs that declare no render-pass phase
      (recognition rule is additive and opt-in per phase). The full pre-existing test suite
      (`test_codegen_entt`: 1643 assertions, `test_semantic`: 479, `test_semantic_modules`: 502,
      `test_parser`, `test_multi_module_integration`, `test_program_linker`, `test_module_artifact`)
      stayed green with unchanged assertion counts throughout this change, across every ordinary
      (non-render-pass) program those suites already covered.

## 8. `examples/gradient-square`

- [x] 8.1 Add `examples/gradient-square/gradient_square.cactus`: one static `pub entity` with
      `WorldTransform` only, a render-pass phase, and `SquareVertex`/`SquareFragment` stage
      handlers per `proposal.md`'s Example section (four fixed per-corner colors; fragment passes
      `tint` straight through). Folded the example's multi-line `vec2(...)` call onto one line —
      this language's indentation-sensitive lexer does not support line continuation inside
      unclosed parens (confirmed empirically; unrelated pre-existing limitation, not something this
      change introduces or is in scope to fix), so `proposal.md`'s literal formatting does not parse
      as written.
- [x] 8.2 Register `example_gradient_square_generated` in `CMakeLists.txt` via
      `cactus_add_example`, mirroring the existing `example_particle_burst_generated` block
- [x] 8.3 Add `CACTUS_EXAMPLE_GRADIENT_SQUARE_SOURCE`/`_TARGET` compile definitions to
      `tests/CMakeLists.txt`, mirroring the particle-burst entries
- [x] 8.4 Add a `gradient-square` curated case to `tests/test_example_cpp_compilation.cpp`,
      mirroring the existing `particle-burst` case
- [x] 8.5 Confirm `examples/gradient-square/gradient_square.cactus` compiles with `--backend
      cpp-entt` and produces valid C++ output including the embedded GLSL pair; the curated
      `gradient-square` compilation-coverage case passes. Both `example_gradient_square_generated`
      and `example_particle_burst_generated` built and linked successfully via the real CMake
      pipeline (not just `cactus.exe` invocation) — genuine end-to-end verification of §4's codegen.
- [x] 8.6 Visual correctness (four distinct corner colors blending smoothly across the square) is
      manually verified, not CI-gated — headless test runs (`CACTUS_RAYLIB_FAKE`) make
      `LoadShaderFromMemory` a permanent no-op, so no pixel is ever sampled under automated
      coverage; same disclosed limitation as `particle-burst`'s fragment falloff math (§7.3).
      Verified with a real GL context via the screenshot-diffing debug harness described in §4's
      bug 5: `gradient-square` renders a square with red/green/yellow/blue corners blending
      smoothly, matching the design; `particle-burst` (directly-spawned entity, bypassing mouse
      input) renders as the expected small soft-edged dot. Debug harness was throwaway and removed
      after use — not part of the shipped test suite.
