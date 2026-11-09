# Solver Limits

The physics solvers stop at three fixed limits: 32 distance iterations, 32 sweep
advance steps and three slide contacts per tick. This document states, for each
limit, why stopping there is safe, what it assumes, what the result is when the
limit is reached, and the evidence.

## 0. Status and authority

This document is an explanatory companion. It does not define behavior. The normative
sources are:

| Topic | Normative source |
|---|---|
| Sweep, touching and their limits | `openspec/specs/stdlib-physics/spec.md` "Solver limits have safe results" |
| The slide limit | `openspec/specs/stdlib-character-body/spec.md` "The slide limit has a defined result" |
| Why a bound must name its assumptions | `openspec/specs/language-philosophy/spec.md` |

When this text and a normative source disagree, the normative source wins.

**The claim is safety, not completeness.** At every limit the result never places a
shape past a contact or through a surface. It may give up progress for one tick.

**Evidence labels.** As in `spec/activation_semantics.md`:

- **Model**: argued here, in exact arithmetic unless the text says otherwise. Not
  machine-checked.
- **Open**: a condition the argument needs that the code is assumed, not shown, to
  provide.
- **Tested**: checked by a named test in the tree.
- **Code**: read from the current source; no test checks it directly.
- **Probe**: a scratch program run on the current tree while writing this document.
  Each probe is described well enough to repeat it.

## 1. Shapes

Every collider is a convex core, a point (`Sphere`), a vertical segment (`Capsule`) or
an oriented box (`Box`), plus a radius (Code: `collider_physics.hpp`). The Minkowski
difference of two cores is therefore a convex polytope with at most 64 vertices. A
box may have zero size on an axis; it is then a rectangle or a segment.

The distance between two shapes is the distance between their cores minus both
radii. Signed distance is negative when they overlap.

## 2. Distance: 32 iterations

`core_distance` is GJK. It keeps a simplex of support points of the Minkowski
difference and its point `v` nearest the origin. Support points are float; the
simplex solve, `v` and the stopping tests run in double (Code).

**Why it stops.** Each iteration either stops or replaces `v` with a nearer point.
The no-progress test makes "nearer" strict in double: when rounding leaves the new
`v` no nearer, the previous simplex is kept and the loop stops (Code). So `|v|²` is a
strictly decreasing sequence of doubles bounded below by the overlap threshold, and
the loop ends whatever the rounding (Model). The other exits are a relative
tolerance on `|v|² - v·w`, a repeated support point, and the origin inside the
simplex.

That argument gives termination, not a useful bound on cost; the 32 cap bounds cost.
In exact arithmetic the distance also strictly decreases, but in float it does not:
before the double solve and the no-progress test, 195 of 60,000 seeded pairs (0.33%)
repeated one state forever, and 3 of them returned a distance up to 0.236 too large
(Probe P1).

**Result when it settles.** `distance` is `|v|`. Over 18,392 apart pairs of the
seeded set, it is at most 9.7e-6 above and 8.8e-7 below the reference distance.
Over 276,021 pairs moved to a gap between 1e-8 and 0.1, it is at most 8.8e-7 above
and 1.6e-6 below (Probe P2). Tested: "core distance is accurate near contact"
(within 1e-5).

**Result at the limit.** `limited` is set and `distance` is `max(0, v·w / |v|)`,
where `w` is the support point in direction `-v`. Every point of the Minkowski
difference lies on the far side of the plane through `w` with normal `v`, so the
true distance is at least that value (Model: supporting hyperplane). A distance that
is not too large keeps every caller safe:

- `touching` compares the distance with the radii, so it never misses a contact.
  Tested: "touching holds at the distance limit for shapes in contact".
- A sweep step `t += gap / closing` is safe when `gap` is not too large (§3).
- `separation` takes its normal from the closest points, normalized by their own
  length, so the normal stays unit length (Code).

Tested: "a distance stopped at its limit is not too large" forces limits 0 to 4 and
compares with a double-precision reference.

The bound can be loose. A loose bound reads as touching, and `push_out` may then
push by up to the radii. That only happens at the limit.

**Is the limit reached?** No sampled query reaches it. The most iterations are 9 over
60,000 seeded pairs and 11 over 1,840,744 near-contact pairs; the no-progress test
ends 0.08% of the near-contact queries and none of the others (Probe P2). Tested:
"an ordinary core distance query reaches no iteration limit" (60,000 seeded pairs and
their near-contact copies; at most 16 iterations).

**Assumptions.** Open: the double solve keeps `|v|` within 1e-5 of the true distance
for shapes outside the tested set. The set covers every kind pair; cubes, slabs, rods
and zero-thickness boxes; free and axis-aligned rotations; and offsets from deep
overlap to 6 units. It is evidence, not a proof.

## 3. Sweep: 32 advance steps

`sweep` is conservative advancement. At the current `t` it takes the signed gap `g`
and the normal `n`, and with the closing speed `c = -delta · n` it moves to
`t + g / c` (Code).

**Why a step never passes a contact.** For convex shapes under translation, the
signed distance `d(t)` is a convex function of `t`, and its slope is `-c` (Model).
A convex function lies above its tangent, so `d(t') ≥ g - c (t' - t)`, which is
positive for every `t' < t + g / c`. So each step ends at or before the first
contact. If `g` is too large by `ε`, the step can end inside by at most `ε`. §2 keeps
`ε` below 1e-5, under the contact skin `kSkin` = 1e-4.

**Why it stops.** It stops when the gap is within `kSkin` (a hit when the motion
closes), when the motion does not close, or when `t` passes 1. Grazing motion
converges slowly, because the closing speed falls as the gap does.

**Result at the limit.** A hit at the last `t` reached. Every `t` reached is safe by
the argument above, so the hit is no later than the true first contact. The body
stops early for that tick. The hit's point and normal come from the last gap
computed, so they are approximate (Code). Tested: "a sweep stopped at its step limit
stops before contact" forces limits 1 to 5.

**Is the limit reached?** Grazing sweeps of a sphere past a sphere, a box edge and a
capsule, at gaps from 1e-1 to 1e-9, take at most 8 steps, because the skin ends them
(Probe P3). Tested: "sweep exits quickly when grazing parallel to a face".

**Assumptions.** Open: the normal is the true direction of steepest gap change, up to
rounding. A box face normal replaces the GJK direction only when it faces the same
way, so a zero-thickness box takes the side the other shape is on (Code:
`facing_box_face`). Tested: "a sweep never passes through a target" runs 300 seeded
pairs with radii and random motions, at the full limit and at limits 1 to 3, and
checks 17 points along each path for overlap beyond `kSkin`.

## 4. Slide: three contacts

A character body sweeps and slides three times per tick (`Slide1`, `Slide2`,
`Slide3` in `stdlib/std/physics/volume.cactus`). Each slide moves the body to the
sweep's `t`, then keeps the rest of the motion with the part into the surface
removed.

**Why three is safe.** Every position change is a sweep result, so §3 keeps every
contact; the limit can only stop the body early (Model). The rest of the motion is
dropped for the tick, and `Integrate` sets `motion` from `velocity` at the start of
every tick (`volume.cactus` line 152), so nothing carries over (Code). `velocity`
keeps the result of the three contacts.

**Why three is not complete.** Each contact projects the motion onto a plane, so its
length does not grow, and the `1 - t` factor shrinks it. Three independent planes
leave no motion. Planes that are not independent can be met again: in a wedge of
half-angle `α`, each contact shortens the motion only by `cos 2α`, so motion is
always left in an acute wedge (Model). The body then stops short for that tick.

**Evidence.** Tested, in `test_character_body_headless_behavior.cpp`:

- "a convex corner stops the body with no overlap": floor and two walls.
- "a fourth contact drops the rest of the tick's motion": a driven body in a
  30-degree wedge keeps motion after three contacts and does not move.
- "dropped motion does not return in the next tick".
- "a body in an acute wedge never passes a wall": 120 ticks.

## 5. Cost

The sweep path runs every fixed tick for every body and solid pair the broadphase
finds. Tested: `test_perf_sweep_benchmark` presses bodies into wedges of rotated
boxes and checks candidates per frame (exact), frame time and growth.

## 6. Probes

Each probe used the helpers of `tests/test_runtime_collider_physics.cpp`:
`sampled_core_pairs` (seed 20261108), `near_contact` and `reference_core_distance`
(pairwise Frank-Wolfe in double over the Minkowski difference's vertices, which
brackets the true distance).

- **P1.** Before the double solve: `core_distance` with limits 32 and 1,024 on
  60,000 seeded pairs, then the reference on each pair that reached the limit.
- **P2.** After it: iterations and `limited` on 60,000 seeded pairs, and on 40 rounds
  of near-contact copies of 50,000 pairs (seeds 0 to 39, gap `10^U(-8,-1)`); the
  reference on 20,000 seeded pairs and on 6 of those rounds; a counter on the
  no-progress exit.
- **P3.** `sweep` of a radius 0.5 sphere from x = -5 by 10 along X, at height `1 - ε`
  past a radius 0.5 sphere and a radius 0.5, height 1 capsule, and at `1.5 - ε` past
  a 2-unit box, for `ε` = 1e-1 to 1e-9.
