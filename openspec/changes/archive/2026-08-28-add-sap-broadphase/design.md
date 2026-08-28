## Context

`SemanticAnalyzer::recognize_spatial_join` (`src/frontend/semantic_analyzer.cpp:4670`) already
recognizes one shape: a pair rule's `where:` clause containing a single, unwrapped `CallExpr` to
`std.collision.flat.circles_overlap`/`std.collision.volume.spheres_overlap`, where every argument
resolves to a member-chain rooted at one of the rule's two pair bindings
(`try_recognize_spatial_predicate`, `resolve_spatial_join_arg`, `semantic_analyzer.cpp:4606-4667`).
A match produces a `SpatialJoinPlan` (`semantic_analyzer.hpp:233`) that `system_emitter.cpp:648-680`
lowers to `SapBroadPhase2D`/`3D` instead of a Cartesian scan. This path is verified end-to-end for
`bouncy-balls-3d` (`test_collision_predicates_headless_behavior.cpp`,
`test_codegen_entt.cpp:6272`).

`bouncy-bubbles`' `DetectBubbleContact` (`examples/bouncy-bubbles/main.cactus:95-135`) has identical
trait-set symmetry (`a`/`b` both require `Bubble`, `CircleCollider`, `tf.WorldTransform`) but rejects
non-overlapping pairs with `if dist >= radius_a + radius_b: return` as the handler body's first two
statements, where `dist = v2m.distance(pos_a, pos_b)` — linear distance, computed in the body, not a
`where:` clause. `recognize_spatial_join` requires `rule.where_clause.has_value()`
(`semantic_analyzer.cpp:4672`) and never looks at the handler body, so this rule silently falls back
to the full directed Cartesian product.

`where:` clauses admit exactly one expression per line with no intermediate `let` bindings
(`spec/cactus_dsl_spec.md:475`), so any manual expression recognized inside `where:` must be a
single self-contained expression tree, not a sequence of bound sub-computations.

## Goals / Non-Goals

**Goals:**
- Close the `bouncy-bubbles` performance gap without changing its simulated behavior.
- Recognize one additional, precisely-specified manual expression shape (squared-distance-via-dot
  compared to squared-radius-sum) inside a `where:` clause, symmetric with how `circles_overlap`
  itself is implemented (`stdlib/std/collision/flat.cactus`: `dot(delta, delta) < radius_sum *
  radius_sum`).
- Give authors a low-noise, actionable signal when they write the specific unaccelerated-distance
  mistake found in the wild, so it doesn't recur silently in future examples or user programs.
- Backfill the `dsl-where-clause` capability spec with the recognition behavior that already ships
  (direct-call form), since it was implemented directly without a tracked spec delta.

**Non-Goals:**
- General algebraic equivalence proving (e.g. recognizing `dx*dx + dy*dy`, expressions spread across
  `let` bindings, or non-canonical operand ordering). Only the specific AST shape in the spec is
  matched; everything else stays an ordinary, unaccelerated predicate with no compile error.
  Extending `where:` to permit intermediate bindings is out of scope — no such change is proposed.
- Recognizing distance-vs-radius-sum guards left in the handler body (as opposed to `where:`).
  `bouncy-bubbles` is fixed by migration instead (see Decisions), so the semantic analyzer never
  needs to reason about body-level control flow for broad-phase purposes. This also sidesteps the
  correctness trap in `dsl-pair-relations`/`CLAUDE.md` around `if`/`return` inside pair handlers.
- A diagnostic for every unaccelerated same-domain pair rule. Only the exact unaccelerated
  linear-distance-vs-radius-sum call shape is flagged (see Decisions) to keep the false-positive
  rate at zero for unrelated pair rules.
- Touching `platformer`'s flat-collider full-scan (`cactus_collect_flat_colliders`) — no `where:`
  clause or recognized-predicate path is involved there; tracked as a separate follow-up.

## Decisions

**Migrate `bouncy-bubbles` instead of teaching the recognizer to read handler bodies.** The
alternative — pattern-matching a distance guard inside the handler body — requires proving it is the
unconditional first statement (otherwise moving it into `where:` changes which tuples run the rest of
the body) and duplicates reasoning `where:` already provides for free. `dsl-pair-relations` already
establishes that a leading `if`/`return` and an equivalent `where:` predicate admit the same tuples,
so rewriting the source is behavior-preserving by the language's own stated semantics, verified by
the existing `test_bouncy_bubbles_headless_behavior` continuing to pass with no assertion changes.
This keeps 100% of the new compiler-side complexity inside `where:`-clause analysis, where the
existing recognizer already lives, rather than adding a second, riskier recognition entry point over
imperative control flow.

**Recognize the manual shape by exact AST pattern, not symbolic simplification.** Mirrors how
`try_recognize_spatial_predicate` already works: a narrow structural match (specific `BinaryExpr`
comparison of a `dot(sub, sub)` call against a squared sum), not a general expression simplifier or
CAS. Alternative considered: normalize/simplify arbitrary arithmetic before comparison (e.g. via a
small symbolic pass) — rejected as disproportionate compiler complexity for a single recognized
idiom, and inconsistent with the direct-call recognizer's existing exact-shape-match precedent.

**Require the dot-product form, not raw component arithmetic.** The recognized manual expression
must call the same `dot` primitive `circles_overlap`/`spheres_overlap` use internally, rather than
also accepting `dx*dx + dy*dy`. Alternative considered: also match component-wise sums — rejected
because it roughly doubles the shapes the recognizer must handle (and doubles again per dimension)
for a form that isn't attested anywhere in the codebase today; can be added later as its own
recognized shape if it shows up in practice, without touching the shapes already specified here.

**Scope the diagnostic to the exact linear-distance call shape, not a general heuristic.** Considered
a broader diagnostic ("any same-trait-domain pair rule without a recognized broad-phase predicate")
but rejected it: most pair rules with symmetric trait domains are not spatial-overlap checks at all
(e.g. simple identity or gameplay-state comparisons), so a broad heuristic would produce hint noise
on unrelated code. Matching specifically on a call to `vec2.distance`/`vec3.distance` between
binding-rooted positions, compared against a binding-rooted radius sum, targets exactly the mistake
this change found in the wild with no plausible false positive.

**Diagnostic reuses the existing `DiagnosticLevel::Warning`, not a new severity tier.**
`error_reporter.hpp` currently defines only `Error` and `Warning` (`error_reporter.hpp:10`); only
`has_errors()`/`error_count()` gate compilation, so a `Warning`-level diagnostic here is already
non-blocking — it cannot fail a build the way the C++ compiler's own `WarningsAsErrors: '*'` treats
`clang-tidy` findings. Considered adding a distinct `Hint`/`Info` `DiagnosticLevel` to signal "purely
a performance suggestion" more precisely — rejected as new shared infrastructure (a new enumerator,
`ErrorReporter::hint()`, a `hint_count()`, `print_summary()` formatting) for what would be its only
call site; `Warning` already conveys "non-blocking, worth looking at" and every other diagnostic
consumer in the tree only branches on `has_errors()`, so no caller needs to distinguish the two
tiers today.

## Risks / Trade-offs

- [Risk] The manual-expression recognizer's exact-shape match is brittle to trivial rewrites (e.g.
  swapping comparison operands, `<=` vs `<`) that are semantically equivalent but textually
  different, so some hand-written predicates that "should" qualify won't. → Mitigation: the hint
  diagnostic only targets the specific linear-distance mistake actually observed, not "unmatched
  manual shape" in general, so missed recognition degrades to a silently-correct-but-slower path
  with no false diagnostic; the spec explicitly documents this boundary as a non-goal.
- [Risk] Migrating `bouncy-bubbles`' source changes the example a reader studies as a hand-rolled
  physics reference. → Mitigation: the existing headless behavior test pins simulated outcomes, and
  the migration is a pure guard-relocation (same comparison, moved from body to `where:`), not a
  logic rewrite — the example still demonstrates the same collision math, just via the recommended
  stdlib predicate.
- [Risk] Future stdlib changes to `circles_overlap`/`spheres_overlap`'s internal implementation could
  silently desync from the manual shape the recognizer expects. → Mitigation: none needed structurally
  — the recognizer matches an AST shape independent of the stdlib function bodies, so stdlib edits
  can't break recognition; only a `.cactus` grammar change to `dot`/subtraction/comparison syntax
  would.

## Migration Plan

Pure additive compiler change plus one example-source edit; no runtime data migration. Land in this
order: (1) semantic-analyzer recognition of the manual squared-distance shape, tested in isolation;
(2) the hint diagnostic, tested in isolation; (3) the `bouncy-bubbles` source migration plus
confirmation its generated code now lowers through `SapBroadPhase2D`. No rollback concerns beyond
reverting the commit(s) — nothing downstream depends on the new diagnostic or recognized shape
existing.
