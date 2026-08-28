## 1. Manual squared-distance expression recognition

- [x] 1.1 Add failing tests in `tests/test_semantic.cpp` (alongside the existing `SpatialJoin`/`circles_overlap`/`spheres_overlap` cases around line 2382) for: a `where:` clause written as `v2m.dot(b.pos - a.pos, b.pos - a.pos) < (a.radius + b.radius) * (a.radius + b.radius)` recognized as `SpatialJoinDimension::Flat2D`; the `vec3`/`v3m.dot` equivalent recognized as `Volume3D`; `<=` accepted as well as `<`; an aliased `dot` import recognized by canonical identity; a mismatched-trait-set pair rejected; and negative cases that must NOT be recognized — component-wise arithmetic (`dx*dx + dy*dy`), the check split across intermediate `let` bindings, and a comparison operator outside `{<, <=}` (e.g. `==`).
- [x] 1.2 Add a `try_recognize_manual_distance_predicate` (or equivalent) alongside `try_recognize_spatial_predicate` in `src/frontend/semantic_analyzer.cpp`/`.hpp`, pattern-matching a `BinaryExpr` comparison (`<`/`<=`) whose LHS is a call to the canonical `dot` function with two identical `(binding.position - other_binding.position)` `BinaryExpr` subtraction arguments, and whose RHS is `(a.radius + b.radius) * (a.radius + b.radius)` with each radius argument resolved via the existing `resolve_spatial_join_arg`. Reuse `SpatialJoinBinding`/`SpatialJoinAccess`/`SpatialJoinDimension` unchanged — this is a second way to produce a `SpatialJoinMatch`, not a new plan shape.
- [x] 1.3 Wire the new matcher into `recognize_spatial_join`'s predicate loop (`semantic_analyzer.cpp:4692-4706`) as a fallback tried when `try_recognize_spatial_predicate` (the direct-call matcher) returns `nullopt` for a given predicate.
- [x] 1.4 Add a failing codegen test in `tests/test_codegen_entt.cpp` (mirroring the existing "SAP-eligible pair rule calls the runtime broad phase instead of a hand-rolled sweep" case at line 6272) confirming a pair rule using the manual dot-product `where:` shape lowers to `cactus::runtime::entt_backend::SapBroadPhase2D`, with no `system_emitter.cpp` changes needed (it already consumes `SpatialJoinPlan` generically).
- [x] 1.5 Get 1.1 and 1.4's tests passing.

## 2. Unaccelerated linear-distance warning diagnostic

- [x] 2.1 Add failing tests in `tests/test_semantic.cpp` for: a `where:` predicate `v2m.distance(a.pos, b.pos) < a.radius + b.radius` producing a `DiagnosticLevel::Warning` diagnostic that names `circles_overlap`; the `vec3`/`v3m.distance` equivalent naming `spheres_overlap`; `>`/`>=` accepted as comparison directions in addition to `<`/`<=`; an unrelated predicate (`a != b`) producing no new diagnostic; and both already-recognized shapes from Section 1 (direct call, manual dot-product) producing no diagnostic. Confirm in each positive case that `has_errors()` stays `false` and compilation output is unaffected.
- [x] 2.2 Implement the matcher: a `BinaryExpr` comparison (any of `<`, `<=`, `>`, `>=`) whose `distance`-call side resolves via `resolve_spatial_join_arg`-style member-chain resolution to two pair-binding-rooted positions, and whose other side is `a.radius + b.radius` with each radius binding-rooted. Call `ErrorReporter::warning` with the predicate's `SourceLocation` and a message naming the dimension-appropriate recognized alternative. Run this check only for predicates that Section 1's two matchers both failed to recognize, so an already-accelerated predicate never also gets flagged.
- [x] 2.3 Get 2.1's tests passing.

## 3. bouncy-bubbles migration

- [x] 3.1 Confirm `tests/test_bouncy_bubbles_headless_behavior.cpp` passes against the current (pre-migration) source as a behavior baseline.
- [x] 3.2 In `examples/bouncy-bubbles/main.cactus`, add `use std.collision.flat as collision`, move `DetectBubbleContact`'s `if a == b: return` and `if dist >= radius_a + radius_b: return` guards into a `where:` block (`a != b`, `collision.circles_overlap(a.tf.WorldTransform.position, a.CircleCollider.radius, b.tf.WorldTransform.position, b.CircleCollider.radius)`), and remove the now-redundant `pos_a`/`pos_b`/`radius_a`/`radius_b`/`dist` computation from the handler body (recomputing only what the remaining collision-response math still needs).
- [x] 3.3 Rebuild the example (rebuild the `cactus` CLI first if stale, per project convention) and confirm it compiles.
- [x] 3.4 Re-run `tests/test_bouncy_bubbles_headless_behavior.cpp` unmodified and confirm it still passes, demonstrating the migration is behavior-preserving.
- [x] 3.5 Extend `tests/test_codegen_entt.cpp` or add a fixture-based check confirming `bouncy-bubbles`' generated code now calls `SapBroadPhase2D` for `DetectBubbleContact`, matching `bouncy-balls-3d`'s existing coverage.

## 4. Docs

- [x] 4.1 Update `spec/cactus_dsl_spec.md`'s "Recognized spatial predicates may be backend-accelerated" paragraph (§3.8.2, around line 511) to also describe the manual dot-product expression shape from Section 1 and the linear-distance warning diagnostic from Section 2, consistent with the `dsl-where-clause` spec delta.

## 5. Validation

- [x] 5.1 Run the full test suite (`ctest`) and confirm no regressions.
- [x] 5.2 Build with `CACTUS_ENABLE_BUILD_CLANG_TIDY` on for touched `src/frontend` files (`semantic_analyzer.cpp`/`.hpp`) and resolve any findings — no `NOLINT` shortcuts per `CLAUDE.md`.
- [x] 5.3 Run `openspec validate add-sap-broadphase --strict` and resolve any findings.
