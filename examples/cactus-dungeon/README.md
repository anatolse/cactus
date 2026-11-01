# Cactus Dungeon

A Dungeon Keeper-like strategy game built from Cactus rules and relations.

Build and run the optimized game from the project root:

```powershell
cmake --preset clang-o3-release
cmake --build --preset clang-o3-release --target example_cactus_dungeon_generated
./build-clang-o3-release/example_cactus_dungeon_generated.exe
```

The 24-by-24 test entry shares its modules with the 85-by-85 game:

```
cmake --build --preset clang-o3-release --target test_cactus_dungeon_headless_behavior
ctest --test-dir build-clang-o3-release -R '^test_cactus_dungeon_headless_behavior$' --output-on-failure
```

## Art

Run `python examples/cactus-dungeon/art/generate_textures.py` to rebuild the
64×64 textures. Lava, water and gold are vertical strips of eight 64×64 frames,
and `visuals.Flipbooks` plays them on the volume with `voxel.TextureFlipbook`.
The script uses only Python's standard library. Its textures are original
project assets, with no third-party inputs.
`python examples/cactus-dungeon/art/generate_models.py` similarly rebuilds the
original heart GLB model and the shared plane for markers and bars.

Robots and knights reuse the CC0 models in `examples/model-renderer/art/`: the
animated robot and the Crispoly mini knight. Units play idle, walk, work, attack,
hit and die clips. The knight has no death clip, so it plays its
`ShieldBlockHighPoseImpact` clip once instead.

## Probe gaps

| Gap | What was needed | Workaround and affected rules | Proposed change |
| --- | --- | --- | --- |
| G1: Relation tests did not index pair iteration | Filter `Walk` rows by common ownership to keep territory links, and derive `Walk` from `Neighbor` over walkable tiles. | `TerritoryLinks` joins walking and ownership rows; worker eligibility and approaches project rows. `WalkLinks` (walkable tile × walkable tile, `where: Neighbor(a, b)`) walked the whole product: in a full-map AI game its pass grew from 1.8 ms to 38 ms per frame as the AIs dug. | Fixed in the backend: a row-stage `R(a, b)` over both entity bindings now walks `R`'s rows from the left binding, in product order. `WalkLinks` stays under 1 ms; 21 dungeon passes use the plan. |
| G2: Two relation rows could not be joined | Join a robot's current tile with `Route` to read the next step and distance, without storing another entity link in a trait. | Bounded endpoint joins are implemented by `add-bounded-relation-row-joins` and exercised in [the routing fixture](../relation_row_joins.cactus). Dungeon workers now use them without a location trait cache. | Implemented and covered by the dungeon routing and mining tests. |
| G3: Generated output fails strict clang-tidy | The generated dungeon program must pass the repository checks. | The completed game exposed 18 findings under the curated generated-code profile: runtime constant initialization, restore dispatcher size, numeric promotion, redundant branches, string ownership and deferred string captures. Compiler regressions cover the emitter fixes, including cached window settings and matching runtime declarations. | All eight regressions pass; the generated dungeon passes the existing curated profile, which excludes only cognitive complexity for generated code. |
| G4: Task identity and work adjacency | Assign the nearest reachable task while preserving both `one` ends of `Claim`. | Resolved for digging: the marked tile is the task; incoming work edges from walkable neighbors let a bounded route choose the approach. `Located` is authoritative and transforms display it. | Endpoint limits are implemented; dungeon digging, mining and invalidation tests pass. |
| G5: Explicit `self` in a removed-trait relation command is rejected | Release a task's claims in its `on removed Task` handler. | `EndWork` removes progress implicitly; `ReleaseClaims` removes canceled claims in the fixed phase. An explicit `remove Claim(_, self)` in the removed handler reports that `self` is outside a handler. | Backend/frontend regression fix pending; no language extension proposed. |
| G6: Aliases and locals can collide with component members | Read `Position.at` through `position`, or match a local copy of `OrderRequest.command` after consuming the field. | `ShowWorkerPosition` uses `request`. The former enum-and-target mailbox was replaced by exclusive request states, which also remove its unused target in None and Cancel. The original spellings generated `WorldTransform.position.at` and matched the cleared component field instead of the local variable. | Backend lexical-scope regression fix pending; no language extension proposed. |
| G7: Projected kept endpoint pairs | Derive the faction-to-neutral-floor frontier from ownership and walking links. | Implemented as `keep Frontier(owner.to, step.to)`, with duplicate witnesses collapsed. The existing task, eligibility, route and work rules handle claiming; no saved frontier is maintained. | The claiming tests pass for multiple supports, cancellation, and both factions. |
| G8: Same-named declarations in separate modules can collide | Use a unit-kind enum alongside `world.Kind`, and a HUD root alongside the viewport Screen. | The unit type is named `UnitKind`, and the HUD root is `HudRoot`. Two enums named `Kind` rewrote terrain references to the wrong module; two entities named `Screen` emitted duplicate constructor helpers. | Backend symbol-identity regression fix pending; no language extension proposed. |
| G9: Public states were missing from CLI source imports | Use the same exclusive order state in world templates and task rules. | Fixed by sharing `ModuleArtifact` public-symbol export between source compilation and artifact loading. No game workaround remains. | CLI and artifact regressions pass; the documented imported-state behavior is restored. |
| G10: Pair binding `target` collided with dispatch internals | Compile ordinary pair rules with `target` on either side. | Fixed the emitter to mangle its recipient variable, using the existing temporary-name helper. | Both binding positions and the complete code-generation suite pass; no language extension. |
| G11: Buffered numeric predicates used const cells | Read accumulated unit and heart health in pair predicates. | Fixed field lowering to request a mutable cell only when settling buffered deltas; other pair reads stay const. | Focused regression and all code-generation cases pass; all 40 dungeon behavior cases pass. |
| G12: Some invalid phase/query accesses survive checking | Diagnose `input.dt` and positional `query.children(self)` before native compilation. | Camera integration uses `tick.dt`; child queries use the documented `query.children[MapMarker](of = self)` form. The invalid forms reached generated C++ before failing. | Frontend diagnostic improvements pending; no language extension proposed. |
| G13: Restored task traits can precede their unsaved relation inputs | Preserve saved progress while task relations rebuild. | `BeginWork` leaves existing progress in place; `EndWork` checks the stored designation and ownership before deleting progress. Claim cleanup respects pause. | Gameplay cleanup now checks authoritative facts; no language extension. |
| G14: Camera views depended on a removed screen singleton | Rebuild view projections after restoring the world. | Fixed the shared camera keeper to read live `ScreenSize` carriers. The persistent HUD root carries window metrics, and measurement refreshes them each input phase. | Both fallback and missing-screen regressions pass; all 14 camera cases pass. |
| G15: A probe output must be initialized as an input | Attach `RayProbe` with its volume and ray, then read only a valid hit. | `PreparePicking` initializes `cell` with the volume handle, matching robot-sandbox. The probe overwrites this unused output before consumers run, and consumers check `hit`. | Output-field initialization analysis remains a language/stdlib design question; no extension implemented here. |
| G16: Delegated scenes selected the wrong camera dimension | Keep game transforms in imported game modules while the entry point declares configuration. | The backend now scans authored modules when the entry point has no transform references, preserving entry-point priority and excluding stdlib implementation details. | Regression checks cover flat, volume, stdlib-only and entry-point scenes. |
| G17: Billboard rendering is a stub | Draw minimap cells and camera-facing health/XP bars. | The public trait has no standard renderer rule, and its runtime submission function only increments a counter. The example uses one shared two-triangle GLB plane, per-view model visibility and derived camera-facing transforms instead. | A complete native billboard renderer and batching remain a backend/stdlib change; no language extension is required. |
| G18: Frame time still grows with territory | Hold 60 FPS on the full map through a ten-minute game. | No workaround. Per-rule timing finds no single hotspot: `ConnectedTiles` (`tile == heart or Reach(heart, tile)`, a disjunction the planner cannot index) grows from 2.4 to 7.7 ms by minute 4, and `BridgeSites`, `AssignTasks`, `AttackTargets` and `BorderCells` each add about 1 ms. `NeighborLinks` re-keeps static adjacency every frame (about 7 ms) and `SenseCells` pairs every unit with every tile (about 7 ms). | Partly fixed in the backend: keep passes queue one command per run, folds use the pass plan, and `Reach(heart, tile)` searches once per heart. Full-map AI frames are 11.6 ms in minute 1 and 26.4 ms in minute 10 (see below). Frame time still grows with territory: `AssignTasks` and the commit grow most. Skipping keep passes whose inputs did not change remains a backend opportunity. No language extension. |
| G19: Standard UI painted only inside active viewports | Show the main menu while both game viewports are inactive. | None possible in game code. The game's own `main()` drew nothing but the white window clear at startup; the rooms and map captures came from a test harness that started play directly, and the earlier menu capture was blank. | Fixed in the backend: a frame with no active viewport still runs the standard UI render rule once in window space. A headless case checks that the menu submits UI primitives. |

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
Manual movement, attack and defend requests use the shared order states and route rows.

## Evidence

Measurements below cover the worker simulation and a complete full-map AI-versus-AI game.

The map tests currently cover seed repeatability (including starting units),
different seeds, the rock border, solid floor layers, the smooth renderer
setting, and a breadth-first check of the diggable route between hearts.
Territory tests cover walk links after excavation, ownership and heart
connectivity for both bases, and bridge sale and rebuilding through stored plans.
The shared room-plan interface validates room and bridge placement, charges
gold, derives room components and capacity, and refunds half on sale. Internal
bridge commands remain available to the territory fixture. The menu, play, pause and save/load flows are implemented and tested.

The earlier 52-case gameplay checkpoint passed at `-O1` (3,757 assertions).
The final optimized gameplay run passes all 66 cases: 65 in the default suite (4,999 assertions in Debug), plus the hidden ten-minute AI case (1,156 assertions). The current code-generation
suite passes 340 cases with 3,147 assertions, including the eight new lint
regressions and the two relation-test pair plan cases. `openspec validate add-cactus-dungeon-example --strict` passes.
Generated-code lint, real-GL captures and the full-map AI game measurement are done.
The full test suite passes, and the coverage gate passes at 94.2% gated line coverage (threshold 90%).

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

XP and stats share one threshold table;
manual movement interrupts work and returns to automatic tasks. Attack and
defend orders drive the shared routing and combat systems. Recruitment reserves a bed and food; knights eat, rest, train for paid XP and
identify intruders. Combat and victory/defeat outcomes are implemented. HUD, minimap, AI, menu flow and persistence behavior are implemented. Generated-code lint, the full suite and the coverage gate pass.

### Full-map AI performance (2026-10-31)

85-by-85 map, two AI players, ten simulated minutes, measured with the hidden
`[.dungeon-ai-performance]` test after the `speed-up-keep-and-join-evaluation`
backend change. Same hardware and `clang-o3-release` build as below.

| Window | Before | After |
| --- | ---: | ---: |
| Game minutes 1-2 | 33.7-37.2 ms | 11.6-13.2 ms |
| Game minute 10 | not measured | 26.4 ms |
| Ten-minute mean / 95th percentile | 47.5 / 64.0 ms | 19.0 / 30.0 ms |
| Structural commands per frame, minutes 1-2 | 37,143 | 72 |

Top rules per fixed tick in minute 2, from timers around each rule call:
commit 2.1 ms, `FloorSurfaces` 1.9 ms, `NeighborLinks` 1.3 ms (was 11 ms),
`WalkableTiles` 0.9 ms, `DigTasks` 0.9 ms, `ConnectedTiles` 0.8 ms (was 2.8-3.4 ms).
By minute 10, `AssignTasks` (4.2 ms) and the commit (4.2 ms) lead; `BorderCells`
and `ClaimableFloor` reach about 1.1-1.4 ms. The game runs at about 50-85 FPS
headless, but frame time still grows with territory, so 60 FPS is not held for
the whole ten minutes.

Reproduce with:

```powershell
cmake --build --preset clang-o3-release --target test_cactus_dungeon_headless_behavior
./build-clang-o3-release/tests/test_cactus_dungeon_headless_behavior.exe '[.dungeon-ai-performance]'
```

### Worker performance (2026-10-29)

85-by-85 map, twenty human robots working on twenty distinct targets. The hidden
`[.dungeon-performance]` test warms eight frames, checks all twenty claims before
and after sampling, and measures the full headless frame. Earth uses 30 frames
so the walls remain intact; Infinite Gold uses 120 frames for continuous work.

| Workload | Mean frame | 95th percentile |
| --- | ---: | ---: |
| Earth excavation | 25.15 ms | 31.62 ms |
| Infinite Gold mining | 24.47 ms | 31.99 ms |

Peak process working set across both workloads: 24,084,480 bytes (22.97 MiB),
sampled through Windows `PeakWorkingSet64`. This includes map creation and test
infrastructure. Hardware: Intel Core i7-8565U, eight logical processors; Windows
11 build 22621; Clang 22.1.6, C++23, `clang-o3-release` (`-O3 -DNDEBUG`).
The fake raylib backend excludes actual GPU rendering. Short samples and host
load limit these results; they establish roughly 40 FPS for this workload,
not the final game's performance or a 60 FPS guarantee.

Reproduce with:

```powershell
cmake --preset clang-o3-release
cmake --build --preset clang-o3-release --target test_cactus_dungeon_headless_behavior
./build-clang-o3-release/tests/test_cactus_dungeon_headless_behavior.exe '[.dungeon-performance]' --durations yes
```

Before the row-query rewrites, the same workloads measured 60.26/58.61 ms mean
and 70.34/72.36 ms at the 95th percentile. All 26 behavior cases still pass
after the rewrites. Local raw logs: `build/dungeon-projected-performance.log`,
`build/dungeon-projected-memory.log` and `build/dungeon-projected-behavior.log`.

Controls: left-click selects, drag selects a group, shift-click toggles a unit,
right-click moves or attacks, and shift-right-click defends. X cancels selected
orders. W/A/S/D, the arrow keys and the screen edges pan, Q/E turn the view in
90° steps, and the wheel zooms within bounds. A drag or a click that starts on a
wall marks it for digging. With any tool other than Select, right-click returns
to Select. UI receives pointer input before world controls. Pending requests stop unit movement and automatic work
before the next order-consumption pass.

Appearance is derived from tile/room kind, bridges, faction ownership and unit
selection. The lower solid voxel copies its logical upper tile's appearance in
late_tick; simulation never reads this display projection. Room rules retain
only room data, costs and ownership. The native captures below validate the world and minimap rendering.

The AI behavior tests drive Decision requests explicitly to check that hidden
cells receive no plans and that an assault requires a discovered hostile heart
and more than four knights. The policy uses only dig marks, room plans,
recruitment requests and shared attack requests. The ten-minute small-map growth test passes (1,156 assertions), using 4,500
frames of 8/60 seconds so each frame stays within the fixed-step catch-up limit.
It verifies changed terrain, lairs, farms and recruitment. It takes about 73
minutes in a Debug build, so it is hidden (`[.dungeon-long]`) from the default
suite and coverage; run it in the optimized build:
`./build-clang-o3-release/tests/test_cactus_dungeon_headless_behavior.exe '[.dungeon-long]'`.
The full-map AI game is measured below.

Save and Load use one slot, `cactus-dungeon`, stored as
`saves/cactus-dungeon.cactussave` relative to the game's working directory.
Save works during play or pause. Load restores gameplay and rebuilds the HUD,
minimap and lighting. The menu offers New Game, Load Game, Options and Exit;
game over offers a return to the menu.

The six flow/persistence cases pass with 1,215 assertions, including a frozen
round trip of work, claims, ownership, room kind, unit XP and health, knight
needs, beds, farm stocks, Brain timing, explored cells and rebuilt views.
Native pointer regressions now cover floor and wall surfaces and UI capture under
the minimap. The native captures below validate the shared plane models.

### Full-map AI versus AI (2026-10-30)

Both factions run the AI on the 85-by-85 map (seed 7, four starting robots each)
for 36,000 active frames of 1/60 second: ten game minutes. One match lasted the
whole run. The test then saves the game. Each line is the mean frame of one game
minute.

| Game minute | Explored cells (both) | Before the G1 fix | After |
| ---: | ---: | ---: | ---: |
| 1 | 1,050 | 35.0 ms | 33.5 ms |
| 2 | 2,131 | 45.1 ms | 39.8 ms |
| 3 | 3,145 | 58.0 ms | 44.1 ms |
| 4 | 3,951 | 80.1 ms | 53.4 ms |
| 5 | 4,622 | 101.0 ms | 58.3 ms |
| 6 | 5,215 | 148.8 ms | 67.9 ms |
| 7 | 5,496 | | 75.4 ms |
| 8 | 5,767 | | 86.1 ms |
| 9 | 5,991 | | 98.3 ms |
| 10 | 6,229 | | 108.8 ms |

After the fix: mean 66.56 ms, 95th percentile 107.53 ms, peak working set
121,507,840 bytes (115.9 MiB), save file 556,208 bytes. The run before the fix
was stopped by the host after six minutes. Frame time still grows linearly with
dug and claimed territory (G18), so the full map stays below 60 FPS. Same
hardware and build as the worker measurement above.

Reproduce from the project root (the hidden test writes its save under
`build/dungeon-benchmark-saves`):

```powershell
cmake --build --preset clang-o3-release --target test_cactus_dungeon_headless_behavior
./build-clang-o3-release/tests/test_cactus_dungeon_headless_behavior.exe '[.dungeon-ai-performance]'
```

Local raw logs: `build/dungeon-final-ai-performance.log`,
`build/dungeon-final-ai-memory.log`, and per-rule timings in
`build/dungeon-probe-rules.log` (before) and `build/dungeon-probe2-rules.log` (after).

### Per-rule profile (2026-10-31)

The same run after the playability rework (walking units, tall walls), with a
timer around each generated rule call. Mean frame 61.2 ms, 95th percentile
105.3 ms, peak working set 128,888,832 bytes (122.9 MiB), save file 1,054,019
bytes. The host was also compiling during game minutes 5 to 8, so those minutes
read high; minutes 1 and 10 ran alone. Mean frame: 35.4 ms at minute 1, 80.0 ms
at minute 10. Milliseconds per frame:

| Rule | Minute 1 | Minute 10 | Shape |
| --- | ---: | ---: | --- |
| `ConnectedTiles` | 2.5 | 22.5 | grows 9× |
| `AssignTasks` | 0.2 | 4.6 | grows 22× |
| `BridgeSites` | < 0.1 | 3.0 | grows |
| `TerritoryLinks` | < 0.1 | 2.1 | grows |
| `BorderCells` | 0.1 | 1.6 | grows 13× |
| `ClaimableFloor` | 0.1 | 1.3 | grows 12× |
| `FriendlyTiles` | 0.1 | 1.3 | grows 19× |
| `WalkLinks` | 0.1 | 1.2 | grows 12× |
| `AttackTargets` | 0.2 | 1.2 | grows 6× |
| `SenseCells` | 8.4 | 7.2 | flat |
| `NeighborLinks` | 7.9 | 7.6 | flat |
| `FloorSurfaces` | 3.3 | 3.2 | flat |

`ConnectedTiles` grows the most. Its `tile == heart or Reach(heart, tile)` runs as
a hearts × tiles product, and both factions' reach grows with every dug tile.
`AssignTasks` and the territory keep rules grow with claimed territory.
`SenseCells`, `NeighborLinks` and `FloorSurfaces` are large but flat: they visit
every tile every tick. The union plan (D24) targets `ConnectedTiles`, and the
grid-region plan targets `SenseCells`. Raw log: `build/dungeon-rules-ai-profile.log`.

### Full-map AI versus AI with pair plans (2026-10-31)

The same ten-minute run in the optimized build after the pair plans, with
nothing else running on the host. Mean frame per game minute:

| Game minute | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Mean frame (ms) | 26.6 | 28.4 | 33.1 | 37.4 | 42.2 | 49.4 | 57.4 | 63.6 | 65.5 | 71.0 |

Mean 47.5 ms (61.2 ms before the plans), 95th percentile 64.0 ms (105.3 ms),
peak working set 128,622,592 bytes (122.7 MiB), save file 1,061,457 bytes. The
full map still does not hold 60 FPS: frame time grows with explored and claimed
territory from the first minute. Raw logs: `build/dungeon-plans-ai-performance.log`
and `build/dungeon-plans-ai-memory.log`.

### Pair plans (2026-10-31)

The compiler now picks one plan per pair rule and warns only when that plan is
the full product. The full game used to print 55 "is not accelerated" warnings;
it now prints 8. No rule was rewritten to silence one. `WalkLinks` and other
`R(x, y)` rules walk relation rows, `ConnectedTiles` and `DigJobs` are unions,
`SenseCells` and `DesignateDig` visit a grid region, and rules whose binding only
the named `Human` and `Enemy` players or the `Overview` rig can fill are bounded
products. The 8 left:

| Rule | Group | Why it stays a product |
| --- | --- | --- |
| `PlaceMinimap`, `ClickMinimap`, `LevelLabels` | c, port rules | Their viewport binding is spawned by templates in `on load` and after a restore. A bound for template spawns needs a proof that each event fires a bounded number of times, which is out of scope. |
| `FightUnits`, `HideOtherLayers` | e, all pairs | Every unit may fight every unit in reach, and every view may hide every model, so each tuple really is a candidate. |
| `EligibleWorkers`, `Defend` | relation test on a row end | `Commands(robot, job.from)` tests a relation against the end of a row binding, not an entity binding; no plan walks that yet. |
| `FightHearts` | distance through a call | `vectors.distance(pose.position, vec3(cell.x + 0.5, …)) <= 1.5` is neither an overlap call between two shapes nor a squared distance over ranged cell fields. |

### Native rendering (2026-10-31)

Real-GL captures at 1280x720 from the optimized game's own window show the
[menu](screenshots/menu.png) and [a new game 20 seconds in](screenshots/play.png):
the CC0 robots, bars and level labels over the home base, tall rough walls,
lava and gold, and the minimap with only explored cells. These captures use
the native runtime, with no fake drawing.

The first menu capture was blank: the game's own `main()` drew nothing at the
menu (G19). The current menu capture comes from the built game after the fix,
and clicking New Game there starts play. The menu now covers the window with
a dark panel, the main view fills the window under the HUD bars, and the menu
has no Main Menu button: Options turns into Back on the options screen.

Flipbooks were checked with real GL (2026-10-31): two captures of a new game
about 2 s apart differ on the lava and gold faces and match exactly on stone
and earth. Captures: `build/flip-a.png`, `build/flip-b.png` and the zoomed
`build/flip-compare.png`.

The generated dungeon passes the repository curated clang-tidy profile with
no new exclusions. Both the full game and small fixture pass. Raw output is in
`build/dungeon-final-main-tidy.log` and `build/dungeon-final-generated-tidy.log`.
After the pair plans, flipbooks and the restore type list, the full game still
passes it: `build/dungeon-plans-main-tidy.log`.
The emitter source checks also report no remaining diagnostics after the fixes.

The source audit found no game-specific extern code or saved route/frontier
cache. Logical tile kind, unit location and ownership drive simulation; voxel
appearance, markers, labels and bars are display projections. Order targets
exist only in states that need them. The one unused probe output placeholder
is documented in G15 and is never consumed without a hit check. Shared command
relations serve both human controls and AI.
