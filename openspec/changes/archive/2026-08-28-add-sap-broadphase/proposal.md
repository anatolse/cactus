## Why

The compiler already has a working sweep-and-prune broad-phase (`SapBroadPhase2D`/`3D`,
`Proxy2D`/`3D`, `sap_execute_pair_tuples`) and correctly routes a pair rule's `where:` clause
through it when that clause calls the recognized `std.collision.flat.circles_overlap` /
`std.collision.volume.spheres_overlap` stdlib predicate — confirmed working correctly in
`bouncy-balls-3d.generated.cpp` and `test_collision_predicates_headless_behavior.generated.cpp`.
But the otherwise-structurally-identical `bouncy-bubbles` example hand-rolls an equivalent
distance-vs-summed-radii check as an `if ... return` guard in the handler body — not even in a
`where:` clause — and silently falls back to an unindexed O(n²) Cartesian scan with no diagnostic.
This is exactly the class of gap this change was evidently scoped for, and it's a real,
checked-in performance regression relative to the sibling 3D example.

## What Changes

- Migrate `bouncy-bubbles`' `DetectBubbleContact` rule to reject non-overlapping pairs through a
  `where: circles_overlap(...)` clause instead of the hand-rolled `if dist >= radius_a + radius_b:
  return` guard that currently opens its handler body. The guard is the body's unconditional first
  statement, so moving it into `where:` preserves behavior exactly (`dsl-pair-relations`: a leading
  `if`/`return` and an equivalent `where:` predicate admit the same tuples) while making it eligible
  for the existing direct-call recognition — no source-visible behavior change, confirmed by the
  existing `test_bouncy_bubbles_headless_behavior` continuing to pass unmodified.
- Extend `SemanticAnalyzer::recognize_spatial_join` to also recognize a manual squared-distance-vs-
  squared-summed-radii expression — built from the same `dot`/subtract/add/multiply primitives
  `circles_overlap`/`spheres_overlap` use internally — written directly in a `where:` clause, so an
  author who inlines the algebra instead of calling the stdlib predicate still gets broad-phase
  acceleration. Scoped to that specific AST shape (a `<`/`<=` comparison of a dot-product-of-deltas
  against a squared sum), not general algebraic equivalence — raw component-wise arithmetic
  (`dx*dx + dy*dy`) or expressions spread across intermediate `let` bindings remain unrecognized.
- Add a compiler diagnostic (hint/info level, not an error) that flags a pair rule's `where:`
  predicate when it calls the *unaccelerated* `std.math.vec2.distance`/`vec3.distance` function
  (linear, not squared) between two pair-binding-rooted positions and compares the result against a
  binding-rooted radius sum — the exact unrecognized shape this change found in the wild — and
  points the author at `circles_overlap`/`spheres_overlap` or the squared-distance form instead.
  Scoped narrowly to that call shape to avoid false positives on unrelated pair-rule predicates.

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `dsl-where-clause`: add a requirement that a pair rule's `where:` predicate SHALL be recognized,
  and lowered through the shared broad-phase implementation, both when it is a direct call to a
  recognized stdlib overlap predicate (already shipped, previously undocumented in this spec) and
  when it is an equivalent manual squared-distance-vs-squared-summed-radii expression built from the
  same primitives. Also add a requirement that an unaccelerated linear-distance `where:` predicate
  comparing two binding-rooted positions against a radius sum SHALL produce a hint diagnostic naming
  the recognized alternative.

## Impact

- Affected: `src/frontend/semantic_analyzer.cpp`/`.hpp` (`recognize_spatial_join`,
  `try_recognize_spatial_predicate`, and the new manual-expression/diagnostic matching around
  the existing call site near line 4071); `examples/bouncy-bubbles/main.cactus`
  (`DetectBubbleContact`'s guard moves into a `where:` clause).
- Out of scope: `platformer.generated.cpp`'s separate flat-collider full-scan
  (`cactus_collect_flat_colliders` and friends) is a related but distinct gap — no `where:`-clause
  pair rule is involved, and there's no existing recognized-predicate path to extend. Left as a
  follow-up candidate rather than folded into this change.
