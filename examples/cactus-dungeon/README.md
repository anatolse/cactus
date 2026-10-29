# Cactus Dungeon

A Dungeon Keeper-like strategy game built from Cactus rules and relations.
Implementation is in progress.

Build the game with `cmake --build build --target example_cactus_dungeon_generated`.
The 24×24 test entry shares its modules with the 85×85 game:

```
cmake --build build --target test_cactus_dungeon_headless_behavior
ctest --test-dir build -R '^test_cactus_dungeon_headless_behavior$' --output-on-failure
```

## Art

Run `python examples/cactus-dungeon/art/generate_textures.py` to rebuild the
64×64 textures. The script uses only Python's standard library. Its textures
are original project assets, with no third-party inputs.

## Probe gaps

| Gap | What was needed | Workaround and affected rules | Proposed change |
| --- | --- | --- | --- |
| G5: Explicit `self` in a removed-trait relation command is rejected | Release a task's claims in its `on removed Task` handler. | `EndWork` removes progress implicitly; `ReleaseClaims` removes canceled claims in the fixed phase. An explicit `remove Claim(_, self)` in the removed handler reports that `self` is outside a handler. | Backend/frontend regression fix pending; no language extension proposed. |
| G6: Event alias can collide with a component member | Read `Position.at` through the alias `position` while assigning `pose.position`. | `ShowWorkerPosition` uses the alias `request`. The original alias generated a read of `WorldTransform.position.at`, which fails C++ compilation. | Backend regression fix pending; no language extension proposed. |
| G4: Task identity and work adjacency | Assign the nearest reachable task while preserving both `one` ends of `Claim`. | Resolved for digging: the marked tile is the task; incoming work edges from walkable neighbors let a bounded route choose the approach. `Located` is authoritative and transforms display it. | Endpoint limits are implemented; dungeon digging, mining and invalidation tests pass. |
| G7: Projected kept endpoint pairs | Derive the faction-to-neutral-floor frontier from ownership and walking links. | Implemented as `keep Frontier(owner.to, step.to)`, with duplicate witnesses collapsed. The existing task, eligibility, route and work rules handle claiming; no saved frontier is maintained. | The claiming tests pass for multiple supports, cancellation, and both factions. |
| G1: Relation tests do not index pair iteration | Filter `Walk` rows by common ownership to keep territory links. | `TerritoryLinks` uses `Walkable` domains, then tests `Walk(a, b)`. It still scans the product of walkable tiles. Copying the coordinate predicate would duplicate the neighbor definition. Full-map timing is pending. | Consider backend planning of pair domains from relation tests; no proposal created. |
| G2: Two relation rows could not be joined | Join a robot's current tile with `Route` to read the next step and distance, without storing another entity link in a trait. | Bounded endpoint joins are implemented by `add-bounded-relation-row-joins` and exercised in [the routing fixture](../relation_row_joins.cactus). Dungeon workers now use them without a location trait cache. | Implemented and covered by the dungeon routing and mining tests. |
| G3: Generated output fails strict clang-tidy | The generated dungeon program must pass the repository checks. | The initial probe reported 23 generated findings (22 cognitive-complexity findings and one boolean-expression finding), plus five in runtime headers. Shared paths have since been fixed for the row-join fixture, which passes strict lint. | Rerun dungeon lint and fix its remaining findings before task 9.4 can pass. |

The minimal routing join is:

```cactus
pairs:
    place: Located
    route: Route
where:
    route.from == place.to
    route.to == Goal
```

The compiler accepts this join and reuses one reverse route search for robots
sharing a goal. The [row-join tests](../../tests/test_relation_row_joins_headless_behavior.cpp)
cover next steps, both binding orders, queued changes and save/restore.
Dungeon digging claims now use endpoint limits and the static `one` proof.
Manual orders and other unit goals remain later tasks.

## Evidence

Headless coverage, real-GL screenshots, full-map frame time, memory and save
size will be recorded here as their tasks complete. No measurements have been
taken yet.

The map tests currently cover seed repeatability (including starting units),
different seeds, the rock border, solid floor layers, the smooth renderer
setting, and a breadth-first check of the diggable route between hearts.
Territory tests cover walk links after excavation, ownership and heart
connectivity for both bases, and bridge sale and rebuilding through stored plans.
Bridge plans are an internal construction queue; room validation and prices are
not implemented yet. The current example is not a playable game.

Current checks (2026-10-29): the headless target builds and the 13 cases for
implemented behavior pass (3,145 assertions). The automatic floor-claiming case and added support/faction cases now pass (23 assertions).
`openspec validate add-cactus-dungeon-example --strict` passes.
`clang-tidy build/generated_examples/cactus-dungeon.generated.cpp -p build --quiet`
fails as described in G3. Its diagnostic log is
`build/cactus-dungeon.clang-tidy.log`. Full-suite tests, coverage, screenshots
and performance measurements remain pending.

### Task assignment gap (G4)

Endpoint limits resolve the claim-proof restriction isolated by the original
scored-row probe. `examples/relation_endpoint_limits.cactus` demonstrates a
bounded `Located(Robot, Tile)` / `Route` join, ranked by `route.distance`, with
free-claim predicates and `limit: 1 per place.from, 1 per route.to`, followed by
`add Claim(place.from, route.to)`. Scored unary candidate rows can likewise
claim their direct endpoints. A separate arrival rule handles a robot already
at its goal, without a self-route, copied location facts or sentinel distance.

The dungeon now uses each marked tile as its task entity. A kept work graph
contains ordinary walking links and incoming edges from walkable neighbors to
valid dig tasks. Solid targets have no outgoing edges, so paths cannot pass
through undug walls. A robot works from its current neighbor when `route.next`
is the target; otherwise it follows the next step. This supports several
approaches to one unique task without a third query binding or location cache.

Validated jobs exclude invalid marks. Kept worker eligibility accounts for
faction ownership, so canceled or foreign-only jobs release their claims.
Earth and Gold become floor, Gold pays its worker's owner, and Infinite Gold
pays smaller amounts more slowly without disappearing. Shared grid adjacency
is defined once in `territory.Neighbor`.

Tasks 3.1–3.3 are complete (10/34 overall). Rooms, leveling, orders, combat, AI, UI
and game flow are not implemented. Full-map performance, strict generated-code
lint, the full suite and coverage are still pending.
