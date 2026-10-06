# Language review drafts (first-person-arena driven)

Status as of 2026-10-03. Arena: 41 → 29 rules, 1374 → 1229 lines since this review.

| Item | Status |
|---|---|
| A1, A2, A5, A6 | done — simplify-dsl-surface |
| A3 | declined — both entity forms stay |
| A4 | closed — superseded by add-rule-groups (modules order against public groups, not rule names; rule-level `after: Rule` stays for ordering inside one module) |
| A7, C3 | done — add-trait-lifecycle-triggers |
| C1 | done — add-deferred-set-command |
| C2 | done — add-named-entity-access |
| C5 | done — add-const-expressions-and-tables |
| C6 | done — add-rule-groups (`group` + `before:`; handler-level group references not planned) |
| C4, C7, C8, S1–S3, R1–R6, B1–B3 | open |

## Follow-up review (2026-10-04)

The language gaps this review blamed for most of the plumbing are closed. What's left in
the arena is mostly engine work written in game code:

```
first-person-arena — 29 rules

 █████████   9  hand-written character controller
               MovePlayer, ApplyJump, BeginGrounding (-1000.0 sentinel),
               IntegrateVerticalMotion, DetectEnemySeparation,
               DetectActorSolidContact, ResolveActorSolidContact,
               DetectGroundCandidate, ApplyGroundCandidate
 ██           2  SpawnRobots / SpawnKnights — 32 lines each, differ only by asset + kind
 █            1  SeekPlayer — now one enemy × wall pair rule with `reduce:` (was two loops over every wall)
 █████████████████  17  gameplay, camera, restart
```

### F1. CharacterBody says it is simulated, but nothing simulates it

`stdlib/std/physics/volume.cactus` and `flat.cactus` say "the backend simulates these
automatically. No user physics rule is needed." Nothing in `src/`, the stdlib, or the
examples reads `CharacterBody`, in 2D or 3D. The platformer and the arena both write their
own physics. This is worse than S2: it's a false promise in the stdlib. It also undercuts
`add-buffered-character-jumping`, which builds on "existing 2D CharacterBody entities".

Until a real controller lands, the stdlib comment should stop claiming automatic
simulation.

- Done 2026-10-05 for 3D (`simulate-character-body`): `std.physics.volume`'s `solve` rules
  move every 3D `CharacterBody`, and the arena uses them. `std.physics.flat` no longer
  claims simulation; a 2D controller still needs 2D shape queries first.

### F2. add-rule-reductions is applied, and it enables S2

Its prerequisite, accelerate-cross-domain-pair-joins, has landed (task 0.1). Reductions
plus a writable retained binding are what let the stdlib write a character controller in
Cactus. That matches how `std.ui` keeps its layout policy in Cactus.

The change adopted reductions only in SeekPlayer. Ground snapping fits just as well:

```
TODAY (4 parts)                          WITH reduce: (1 rule)
BeginGrounding: surface = -1000.0        rule GroundActor:
DetectGroundCandidate (pair)               pairs: actor × surface
  └─ emit GroundCandidate ──┐              where: <the same loose bound>
ApplyGroundCandidate        ◀┘             reduce: per: actor
  if candidate > surface: snap               found = any(<exact test>)
                                             top   = max(<top>, default = 0.0)
                                           on fixed_tick:
                                             actor.grounded = found
                                             if found: snap to top
```

Outer groups make "no ground under me" a real row with `found = false`, so the sentinel
goes away. This removes 2 rules, 1 event, and the sentinel. Solid-contact resolution
(`sum(separation)` per actor) would fit too, but it needs a `vec3` sum. The design only
types `count`/`sum` as numeric, so that stays an open question.

### F3. Stale backlog

| Change | State | Take |
|---|---|---|
| add-buffered-character-jumping | 0/11, untouched since 2026-09-06 | its base doesn't exist (F1); fold it into a character-controller change |
| add-rule-execution-explanations | 0/13, untouched since 2026-09-06 | useful tool, not urgent; park |
| flatten-ui-layout-handlers | no tasks | recheck options now that rule groups and lifecycle triggers exist |
| evolve-cactus-notation | 35/47 | waiting on approval to spend model tokens (8.1); most of the residue it targets has since been fixed by hand |
| add-cir-output | code only on `cir_attempt` | still unresolved |

### F4. Remaining language items, re-ranked

- **C4 `template_ref`** is now the clearest duplication. `Enemy.kind` already points into
  a const table (`ROBOT.run_clip`, …). If that table could also hold the template and
  model asset, SpawnRobots and SpawnKnights become one rule.
- **S3 named clips.** `run_clip = 6  # Robot_Running` breaks silently when an artist
  re-exports a model.
- **C7 numeric `for` / `continue`** drops in priority. Its main evidence was SeekPlayer's
  flags, which reductions remove. Procedural generation still wants it.

### Proposed order

1. Apply add-rule-reductions, and extend its arena adoption to ground snapping (F2).
2. New change `simulate-character-body`: make the stdlib CharacterBody claim true
   (volume first, then flat) with stdlib rules written in Cactus using reductions. It
   absorbs add-buffered-character-jumping. The arena and platformer drop their
   hand-written controllers.
3. complete-physics-contact-lifecycle (done 2026-10-05): `keep` clauses, `best`, `Solid`
   triggers and `physics.contacts`; `CollisionEnter` is gone.
4. C4 `template_ref` + S3 named clips.

Open question: should the controller live as stdlib Cactus rules (recommended) or as
shared C++ runtime code?

Original review text follows unchanged except for per-item status markers.

  ...limit/reduce), the in-flight changes, and the two largest games: first-person-arena (1374 lines) and
  split-screen-forest-bombs. The arena is the best evidence, so I'll start there.

  Where the authoring cost actually goes

  I sorted the arena's 41 rules by why each one exists:

  first-person-arena/main.cactus — 41 rules

   ████████████  12  "apply" rules: they only exist to write a field on ANOTHER entity
                     (ApplyCameraPose, ApplySetEnemyClip, ApplyEnemyDeathProgress,
                      Show/Hide*Label x4, RestartClears* x2, RestartResets* x2, ...)
   ███████        7  hand-written character controller (grounding, gravity, wall push,
                     the -1000.0 sentinel) — std.physics.volume.CharacterBody says it
                     "simulates automatically", but the game could not use it
   ██             2  copies that differ only by kind (SpawnRobots/Knights,
                     BeginRobotDeath/KnightDeath)
   ████████████████████  20  actual gameplay

   plus: the "is the game over?" check written 9 times
         (4x query.first[Player] + match + let-flag, 5x `if game_over: return`)
   plus: an `enemy.dying` bool checked in 11 places, where a Dying marker trait would do

  So about half the rules are plumbing. Three language gaps cause most of it:

             ┌─────────────────────────┐
             │ can't write to entity X │──▶ emit Foo to X ──▶ rule ApplyFoo: on Foo: x.f = ...
             └─────────────────────────┘
             ┌─────────────────────────┐
             │ no global game state    │──▶ query.first[Player]() + match + let flag, again and again
             └─────────────────────────┘
             ┌─────────────────────────┐
             │ no hook when a trait is │──▶ bool flags (dying, consumed, exploded) +
             │ added/removed           │    `if flag: return` in every rule
             └─────────────────────────┘

  ---

  Part A: Things to remove or merge

  A1. [DONE — simplify-dsl-surface: `let` locals immutable, `var` mutable] let and var in handler locals mean the same thing. The analyzer doesn't enforce let. The arena reassigns let
  many times (let active = false … active = not player.game_over, and let model_scale). Right now the two keywords
  carry one meaning. Recommendation: make let actually immutable. That's more useful than dropping var: RTS and
  simulation code gets easier to read when "doesn't change" is guaranteed. If you want the smallest grammar instead,
  drop var from locals. Either way, the current state is misleading.

  A2. [DONE — simplify-dsl-surface] Remove sync. It has no semantics: there's no networking, and the spec never says what it does. It appears only
  in platformer and cactus_shop, always next to persist. It reads as a promise the language can't keep. Bring it back
  with a real networking design (see R6).

  A3. [DECLINED — simplify-dsl-surface: `use` is open composition, `from` is closed; both stay] Merge the two ways to base a top-level entity on a template. entity X: use T and entity X from T: are both
  load-time and both create one entity. The spec needs a paragraph to explain FirstWalker vs SecondWalker. Keep from
  for instances, since it also supports children: overrides by role. Allow body-level use only inside templates, where
  it works as a mixin. You lose nothing, and you remove one decision a beginner has to make.

  A4. [CLOSED — add-rule-groups: cross-module ordering goes through public groups; rule-level after: Rule stays for same-module order] Keep one ordering mechanism. Right now there are three: rule-level after: (the spec calls it legacy),
  handler-level after:, and implicit writer-before-reader ordering. The arena uses rule-level after: 6 times to name
  other rules. Naming rules couples modules to each other's internals. Remove rule-level after:. See also C6 below,
  which would replace most remaining after: uses.

  A5. [DONE — simplify-dsl-surface] Remove lambdas and .map/.filter/.reduce pipelines. LambdaExpr and PipelineExpr exist in the AST, but I found
  exactly one use in all .cactus code (cactus_shop/shop.cactus:91). They add a second aggregation model next to the
  planned reduce: clause (the add-rule-reductions change), and they widen the imperative tier that language-philosophy
  wants to keep small. List aggregation that is really needed (any, sum over a query snapshot) can be a few fixed
  stdlib functions.

  A6. [DONE — simplify-dsl-surface: renamed to std.core.KeepOnLoad] Rename std.core.Persistent. This isn't a removal, but it removes confusion. §7.6 needs a whole paragraph
  ("Persistence is not scene survival") only because persist and Persistent sound alike and mean unrelated things.
  Rename it to KeepOnLoad or SurvivesSceneLoad and delete that paragraph.

  A7. [DONE — add-trait-lifecycle-triggers: spawn/destroy removed, replaced by on added/on removed] Either make the spawn/destroy lifecycle events work or delete them. std.core declares pub event spawn and pub
  event destroy, but nothing in the repo handles them, and the spec says the runtime never synthesizes events from
  their names. They are dead names today, and they also take up two keywords as event names. I recommend making them
  work (see C3).

  Not a language problem, but worth noting: some of the arena's size is the example lagging behind the language. It
  has 22 box entities of 9 lines each, and each repeats scale again as BoxCollider.size. Parameterized templates
  (proposal 006, already implemented) would cut about 200 lines today: entity NorthWall from WallBox(pos = ..., size =
  ..., tint = ...). Also, RestartClearsEnemy/RestartClearsBullet could already be destroy enemy inside the loop. Fix
  the examples before blaming the language for these.

  ---

  Part B: Things to add

  Cross-cutting (every genre benefits)

  C1. Deferred writes to another entity — DONE (add-deferred-set-command: `set T on e:`, last writer wins per field)
  cactus
  for visual in query.children[EnemyVisual](of = self):
      set models.ModelAnimator on visual:      # sketch syntax
          clip = ROBOT_DEATH_CLIP
          time = 0.0
  This is a buffered field patch that commits at the activation boundary, like add … to e:. The difference is that it
  updates an existing trait instead of replacing it. Its handler contract is a declared write on ModelAnimator, so
  scheduling stays fully analyzable.

  - Removes the 12 apply rules in the arena and their 8 single-use events.
  - Shooter: damage, knockback, pickups.
  - RTS: an order issued to selected units.
  - Sandbox: a tool edits the block or entity it targets.
  - Open question: how two sets on the same field in one activation resolve. Options are last-writer-wins in
    deterministic command order, or a compile error. Talk this through before proposing.

  C2. Singleton state ("resources") — DONE (add-named-entity-access)
  Shipped without a singleton keyword: game-wide state lives on an ordinary named entity, read and written by
  name (`Game.Match.over`), and rules switch off with `when:`. A missing entity just stops the dependent
  handlers. The arena now keeps its game-over state on `entity Game`. See spec §3.8.4 and §4.2.

  Original sketch:
  cactus
  singleton trait Match:            # exactly one, created by the runtime
      var over: bool = false
      var wave: int = 1

  rule SeekPlayer:
      filter: ...
      when: not Match.over          # sketch: rule-level gate, pure, like where:
  - Removes the 9 copies of the game-over check.
  - Enables pause, menu → playing → game-over flow, wave counters, score, difficulty, time-of-day, and RTS player
    resources (gold, supply).
  - Today the answer to "is the game paused?" is 6 lines in every rule.
  - Reads fit the existing contract model directly, because a singleton is just a trait with one known entity.

  C3. Trait lifecycle triggers — DONE (add-trait-lifecycle-triggers: `on added T` / `on removed T as old`)
  cactus
  rule StartDying:
      filter:
          Enemy
      on added Dying:               # fires after the commit that added it
          set_clip(...)

  rule SeekPlayer:
      filter:
          Enemy
      exclude:
          Dying                     # replaces 11 `if enemy.dying` checks
  Also on removed T and on spawned (gives the dead spawn event a real meaning).

  - The spec already recommends add/remove as the state-change model (§4.4). Games avoid it because nothing lets them
    react to an entry or exit, so they fall back to bool flags.
  - Enables marker-trait state machines (Idle/Chasing/Attacking), setup when an entity spawns (the spawner doesn't
    need to compute model_scale), and cleanup on death.

  C4. Templates as values
  cactus
  trait Spawner:
      let template: template_ref      # sketch
  spawn spawner.template(position = transform.position)
  - Removes the SpawnRobots/SpawnKnights and BeginRobotDeath/KnightDeath copies.
  - RTS: production queues, build menus, tech trees.
  - Shooter: weapon definitions, wave tables, loot drops.
  - Sandbox: a block palette.
  - This conflicts with §3.7 ("templates are not ordinary values"). Allow only a restricted template_ref: no fields,
    it only works in spawn, and arguments are type-checked against a shared parameter signature. That keeps static
    analysis intact.

  C5. Const expressions and const tables — DONE (add-const-expressions-and-tables: const expressions, typed constants, struct values, const lists)
  Today const_value must be a literal, so the arena writes ENEMY_STEER_ANGLE_SMALL = 0.6981317 instead of deg(40.0),
  and HALF_PI = 1.57079633. Two steps:
  1. Pure compile-time expressions in const:.
  2. const structs and lists: const UNITS: list[UnitDef] = [...].

  - RTS: unit stat tables, build costs.
  - Shooter: weapon stats, wave definitions.
  - Sandbox: block properties, crafting recipes, loot tables.
  - This is where game balancing happens. It also pairs with C4.

  C6. Named stages inside a phase — DONE (add-rule-groups: `pub group Name: phase: ...`, `group:`, `before:`, groups in `after:`)
  cactus
  pub phase fixed_tick:
      ...
      stages: move, collide, resolve     # sketch
  rule IntegrateVerticalMotion:
      on fixed_tick.move:
  - Replaces the arena's after: MovePlayer, SeekPlayer, DetectEnemySeparation chains. Rules would name a stage instead
    of each other.
  - Stdlib physics would publish its stages, so user code can run "after physics" without knowing stdlib rule names.
  - This is the ordering fix that A4 needs.

  C7. Bounded numeric for and continue
  Language-philosophy rules out numeric for because it's "open-ended". But for i in 0..N with a const or limit-style
  bound is exactly as bounded as for x in list. In SeekPlayer, the five *_blocked flags and if not x_blocked and …
  guards are a workaround for having no continue or break. Both are still bounded. I think that rule deserves a second
  look.
  - Procedural generation needs this: place 200 trees, fill a chunk, spawn a wave of N. Forest-bombs writes out 6
    trees by hand at 18 lines each.

  C8. RNG that advances itself
  Every sample today is two lines, rng = rand.advance(rng) then rand.sample(rng, d). If you forget the first line, you
  silently get the same number again. Make an Rng trait field a writable place: let r = rand.next(rng,
  rand.uniform(0.0, 1.0)) advances it in place. Add value noise or Perlin (rand.noise2(seed, p)).
  - Sandbox terrain, particle variation, AI randomness, loot.

  Shooters

  S1. Shape-accurate raycast with hit data. std.physics.volume.query.raycast only returns an entity_id and only checks
  the candidate's center (a known gap). The flat version already has QueryResult2D. Add raycast returning
  QueryResult3D {other, point, normal, distance}.
  - Hitscan weapons, line-of-sight AI.
  - Ground probing would replace the arena's DetectGroundCandidate pair rule.
  - Obstacle probing would replace SeekPlayer's five hand-written probe loops over every wall.
  - Done 2026-10-04 in shape-agnostic form (`add-volume-shape-queries`): `physics.sweep(a, delta, b)` and
    `physics.touching(a, b)` between rule bindings, with `first_hit` in `reduce:`. A probe is a sweep of a small
    collider; there is no separate raycast. Arena bullets use it.

  S2. A 3D character controller that actually works (move_and_slide). The stdlib says it simulates this, but the
  flagship 3D game rewrote it in 7 rules with a sentinel value. That gap is the strongest signal in the repo. Build it
  in the stdlib: step height, slopes, jump buffering (add-buffered-character-jumping is in flight), and separation
  between actors.
  - FPS, TPS, 3D platformers, and any sandbox with a walking player.
  - Done 2026-10-05 for 3D (`simulate-character-body`): gravity, sliding, step-up and snap-down, slope limit,
    `time_since_grounded`, and body separation, as stdlib rules on `sweep`, `first_hit` and `push_out`. Still open:
    a 2D controller and 2D shape queries, and a jump-buffer policy (add-buffered-character-jumping is kept).

  S3. Named animation clips and simple animation state. ROBOT_RUN_CLIP = 6 is a magic index with a 10-line comment
  explaining where it comes from. Resolve clips by name at load time: models.clip(Robot, "Robot_Running"). Later, add
  a small AnimState trait with crossfade.

  (Contact enter/exit is done by complete-physics-contact-lifecycle: a `keep` rule in `physics.contacts` keeps a
  trait while a contact lasts, and `on added` / `on removed` are enter and exit. `CollisionEnter` is removed.)

  RTS

  R1. Radius-based neighbor domains that can be accelerated. pairs: is an N×M cross product. Broad-phase acceleration
  only recognizes circles_overlap/spheres_overlap on collider radii. RTS needs "units within my attack range" (range ≠
  collider size), for 500+ units. Extend the recognized shapes to within(a.pos, b.pos, a.Attack.range), or make it an
  explicit near: domain.
  - Target acquisition, aggro, unit separation, aura buffs.
  - Without this, RTS scale doesn't work.

  R2. Navigation (std.nav). Grid or navmesh pathfinding as an extern rule: emit nav.PathRequest to unit: goal = …,
  answered by on nav.PathReady. This follows the persistence pattern (request event → outcome event).
  - RTS movement, tower defense, shooter AI (replaces SeekPlayer's steering).

  R3. Box selection and screen-to-world. std.pointer only picks a single target. Add drag-rectangle selection
  (query.in_screen_rect[Selectable](min, max, camera)) and camera.screen_to_ground(pos).
  - Unit selection, building placement, editor marquee (std.editor would also use it).

  R4. Rule rate control. Periodic custom phases already exist (every:), so "AI thinks at 4 Hz" is possible today.
  That's a strength; document it as a recipe. What's missing is staggering: spreading N entities across frames, every:
  0.25 stagger. It's cheap to add later.

  R5. Relations beyond Parent. Bomb.owner, TimerOwner.owner, and Weapon.reload_timer are unindexed entity_id fields.
  Add a reverse lookup: query.referencing[Bomb.owner](of = self), meaning "all bombs owned by me".
  - RTS squads, garrisons, "who is targeting me".
  - Sandbox inventories (item → owner), attachments.

  R6. Lockstep determinism, a future bet rather than now. Cactus already has what lockstep multiplayer needs:
  creation-order snapshots, deterministic command buffers, and a fixed-step phase. The missing part is deterministic
  math (fixed point, or strict float with no fast-math, which time.cactus already requires). This is the right reason
  to delete sync now (A2) and design networking later around lockstep rather than per-field sync.

  Sandbox

  B1. Grid and chunk storage (std.grid). One entity per tile doesn't scale: a 256×256 map is 65k entities, and pair
  rules over them are impossible. Add a typed cell store:
  cactus
  grid Terrain: cell = Block, size = (256, 256), chunk = 16    # sketch
  Terrain.at(ivec2(x, y)).kind = Block.Stone
  on Terrain.changed as c: ...
  - Voxel and tile worlds, farming sims, fog of war and influence maps (RTS), the pathfinding substrate for R2,
    destructible terrain.
  - It's also the biggest item on this list. It needs an integer vector type (ivec2/ivec3) and persistence
    integration.

  B2. Integer vectors. vec2(474.0, 252.0) for screen pixels, and float grid coordinates everywhere. ivec2/ivec3 are
  needed for B1, tile coordinates, and UI.

  B3. Runtime-built meshes (later). Chunk meshing for voxel worlds needs "build a mesh from data". This probably
  belongs in an extern-rule stdlib module, not in the language.

  ---

  Priority matrix

  ┌───────┬────────────────────────┬─────────┬─────┬─────────┬──────┬─────────────────────┐
  │   #   │        Proposal        │ Shooter │ RTS │ Sandbox │ Cost │  Arena lines saved  │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ C1    │ deferred remote writes │   ●●●   │ ●●● │   ●●●   │  M   │ ~12 rules, 8 events │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ C2    │ singleton + when:      │   ●●●   │ ●●● │   ●●●   │  M   │ 9 gate blocks       │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ C3    │ on added/removed       │   ●●●   │ ●●  │   ●●    │  M   │ 11 dying checks     │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ S2    │ working 3D controller  │   ●●●   │  ●  │   ●●●   │  L   │ 7 rules             │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ C4    │ template_ref           │   ●●    │ ●●● │   ●●    │  M   │ 2 duplicate rules   │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ C5    │ const exprs + tables   │   ●●    │ ●●● │   ●●●   │ S→M  │ readability         │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ S1    │ raycast w/ hit data    │   ●●●   │ ●●  │   ●●●   │  S   │ probe loops         │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ A1–A7 │ removals and merges    │    —    │  —  │    —    │  S   │ spec shrinks ~10%   │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ C7    │ numeric for / continue │    ●    │ ●●  │   ●●●   │  S   │ SeekPlayer flags    │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ R1    │ radius neighbor domain │    ●    │ ●●● │    ●    │  L   │ —                   │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ B1    │ grid storage           │    —    │ ●●  │   ●●●   │  XL  │ —                   │
  ├───────┼────────────────────────┼─────────┼─────┼─────────┼──────┼─────────────────────┤
  │ R2    │ navigation             │   ●●    │ ●●● │   ●●    │  L   │ steering code       │
  └───────┴────────────────────────┴─────────┴─────┴─────────┴──────┴─────────────────────┘

  C1, C2 and C3 together likely cut the arena by a third. None of the three weakens static analysis: each keeps writes
  declared, reads visible, and structural changes at commit boundaries.

  Where I'd push back on current principles

  1. "No numeric for, no break/continue" mixes up bounded with collection-driven. A const-bounded range is as
     analyzable as a list, and continue can't make a loop unbounded.
  2. "Templates are not values" blocks the most common data-driven game pattern (spawn tables). A restricted
     template_ref keeps the guarantee this rule was protecting.
  3. "Pair bindings are read-only" is right for pairs. But the same limit applying to all cross-entity writes (no C1)
     is what produces the emit/apply pairs. Your own evolve-cactus-notation proposal already names this as "imperative
     residue".

  Threads worth following

  - C1 and C2 may be one feature. A singleton is "a remote write to a known entity". If C1 lands, C2 might just be
    singleton trait plus a when: gate.
  - The C3 vs project overlap. Projected traits are also frame-local state markers. Should on added fire for
    projections? My guess is no, but it needs a decision.
  - The notation-evolution harness could score these. You have 12 katas and an imperative-residue metric. C1–C3 target
    exactly the residue categories it counts (emit/apply splits, sentinels, rule-name ordering). Running the ancestor
    against a hand-written "C1+C2+C3" variant would test this review with numbers instead of opinion.
## Recap (2026-10-04, re-evaluation)

- **F2 corrected.** Reductions alone only make "overlap, then push out" shorter, and
  that approach double-pushes in corners, can't slide, and tunnels. The missing piece is
  a first-hit sweep. 2D has one (`query_cast_nearest`); 3D doesn't. That is why the
  arena needs 9 controller rules.
- **Stay inside the rule algebra.** Extern world queries hide reads from the scheduler
  and planner. Instead: shape-agnostic `physics.sweep(a, delta, b)` /
  `physics.touching(a, b)` over rule bindings, plus a `first_hit` reducer. The author
  names roles, not shapes. The compiler derives broad-phase bounds and picks kernels and
  acceleration from whole-program knowledge.
- **Philosophy made explicit** (`state-error-prevention-and-whole-program-performance`):
  the language rules out common errors by construction; the backend uses whole-program
  knowledge for performance; authors never restate derivable facts.
- **Don't adopt reductions for arena ground snapping.** The controller change will
  delete that code.
- **Controller split:** geometry kernels in the C++ runtime; the controller itself
  (slide stages, gravity, jump buffer, coyote time) as stdlib Cactus rules on top of
  `sweep` + `first_hit`.
- **New order:**
  1. `state-error-prevention-and-whole-program-performance` (proposed): philosophy
     update + `CLAUDE.md` line.
  2. `add-volume-shape-queries` (done 2026-10-04): `sweep`/`touching`/`first_hit`, collider
     proxies in the planner, GJK kernels in the runtime, `fixed_tick.dt` as a
     constant, one-shape rule; arena bullets become one pair rule. Checked for step 3:
     a later `fixed_tick` rule sees an earlier rule's `WorldTransform` write in the same
     activation, in unary and reduced pair rules.
  3. `simulate-character-body`: `move_and_slide` as stdlib rules; absorbs
     add-buffered-character-jumping; the arena and platformer drop their controllers.
  4. complete-physics-contact-lifecycle (done 2026-10-05): kept contact traits, `best`,
     `Solid` and `physics.contacts`; `CollisionEnter` and its min-corner AABB pass are removed.
  5. C4 `template_ref` + S3 named clips.
- **Cleanup:** fix the "simulates automatically" comment in `std.physics.*`; archive
  harden-notation-evolution-harness; close or park evolve-cactus-notation,
  flatten-ui-layout-handlers, optimize-ui-layout-caching, add-rule-execution-explanations.

## Tools-profile gaps (2026-10-05, broaden-language-identity)

UI apps and editors are now in scope, and application logic forced into `extern` code
counts as a language gap. Known gaps:

| Gap | Evidence | Likely primitive |
|---|---|---|
| Templates by string name | `std.editor.spawn_template(template_name: string, …)` | `template_ref` (C4) |
| Inspector in native code | `EditorPropertyPanel` is an extern stub | compile-time reflection of traits and fields |
| Palette and list UI by index | `palette_label_slot(index)`, `palette_button_y(index)` | derived entities: one row per source entity |
| Layout as one imperative loop | `std.ui` `MeasureUi` walks `hierarchy_postorder` with `match` chains | tree folds over `Parent` (derived values) |
| Every UI field spelled out | `examples/standard-ui` repeats `visible`/`enabled`/`z_index` per node | check that declared trait defaults apply |
| Strings and collections underspecified | §4.6 limits string literals to `const`; `list` trait fields have no stated semantics | value-type strings, lists, maps |
| Undo and play-in-editor | no transaction or world-fork model | command-log transactions, world capture/restore |

## Language review (2026-10-05)

Role: language designer. The notation comes first and the backend second. Goal:
simple, declarative-first definitions with imperative handlers and pure functions,
for any genre and scale, including UI apps such as an editor. Memory, lifetime and
data-race errors must be impossible by construction.

### What to keep

| Mechanism | What it buys |
|---|---|
| Inferred handler contracts | Data-race freedom with no access annotations |
| Rule algebra (filter, pairs, where, reduce, order, limit) | A relational query language; the planner sees every join |
| Total `entity_id` + deferred structural commits | No dangling handles, no iterator invalidation, deterministic order |
| `keep` (single-writer derived trait) | The first truly declarative derived state; the direction to grow |

### Diagnosis

The language grows by accretion. Each arena gap got a new clause with its own
special rules. A rule has 12 optional clauses in a fixed order, and the spec is about
1,900 lines. There are also seven ways to change state, with three different timings:

```
x.f = v           immediate      self / binding
Game.T.f = v      immediate      named entity
set T on e:       deferred       last writer wins (silent)
add / remove      deferred       net change per round
project T         frame overlay  coalesced
keep T on b       derived        exactly one writer
emit E to e       queued         —
```

Most game and UI state is a function of other state: grounded, in contact, layout
size, effective visibility, health-bar position, outliner rows. Authors maintain it
by hand with flags, countdowns, apply rules and reconcile loops, and every
hand-maintained copy can drift. Generalizing derived state is the biggest lever.

### Error classes

| Class | Today | Gap → fix |
|---|---|---|
| Dangling handle | ✅ total `entity_id`, deferred destroy | silent no-op hides bugs → `on lost` for weak refs |
| Leaks / orphans | ◐ `Parent` cascade, `SceneCleanup` | non-Parent refs don't own → `owned` refs; UI mirrors → derived entities |
| Data races | ✅ inferred contracts, conflict serialization | `set` last-writer-wins is silent → diagnose or declare a merge |
| Iterator invalidation | ✅ membership snapshots | — |
| State desync (flags) | ◐ `keep`, `on added` / `on removed` | exclusive states, derived state |
| Time bugs | ◐ constant fixed `dt`, phases | `timer` field type |
| Nondeterminism | ✅ creation order, fixed fold order | manual RNG advance (C8) |
| Stringly-typed links | ❌ `spawn_template("name")`, `clip = 6` | `template_ref`, named clips, reflection |

### Corrections, checked against the compiler

- `else if` already shipped (`b5412d3`). It is now documented in spec §3.16 and used in
  the arena (`07e7c0a`).
- `if` and `match` expressions and the value `match` statement now lower to C++ and
  GLSL, and conditions must be `bool` (`complete-conditional-expressions`).
- Declared trait defaults are applied. `examples/standard-ui` is verbose only because
  it follows an outdated comment; that is example cleanup, not a language gap.
- Before calling a feature missing or working, grep `src/` and `tests/`, read the
  capability specs, and compile a probe.

### Open items, ranked

Done: identity rewrite (`broaden-language-identity`, `ea2b35f`); conditional
expressions (`complete-conditional-expressions`).

| # | Item | Solves | Size |
|---|---|---|---|
| 2 | Exclusive states | contradictory marker traits; state machines in AI, UI and game flow | M |
| 3 | `timer` field type + `on elapsed` | hand-written countdowns; gives the backend every deadline | M |
| 4 | Derived entities + `keep` on unary rules | UI lists, outliners, health bars, palettes; leaks and duplicates | L |
| 5 | Tree folds over `Parent` | `std.ui` layout as one imperative loop; hierarchy totals | L |
| 6 | Typed refs and relations (`owned` / `weak`, join through a ref) | silent stale handles, ownership, one-Timer-per-entity, reverse lookup | L |
| 7 | Write semantics: defer every cross-entity write; diagnose `set` conflicts | two timings for one action; silent lost writes | M |
| 8 | Values and reflection: strings, collections, `template_ref`, `std.reflect` | editor logic in extern C++ (see "Tools-profile gaps") | L |
| 9 | Removals: `limit … per` writability, authored `project`, gameplay `query.*` in handlers, cross-module rule-name ordering, bare field access | fewer special-case rules, smaller spec | S–M each |

Items 2–9 are not yet checked against `src/` and `tests/`; check each one before
proposing it.

### Sketches (syntax not decided)

```cactus
# 2. Exclusive states: adding one removes the others in the same commit
state EnemyMode: Idle, Chasing, Attacking, Dying

# 3. Time as a type: the runtime counts it down
trait Shooter:
    var cooldown: timer
rule Spawn:
    on elapsed SpawnPoint.countdown: ...

# 4. Derived entities: one child per source row, keyed by the source entity
rule HealthBars:
    filter:
        Enemy
        Health as hp
    keep entity Bar under HudLayer:
        ui.Progress:
            value = hp.health / hp.max_health

# 5. Tree fold: bottom-up over children, order derived from tree depth
rule MeasureStack:
    filter:
        ui.Stack as s
    reduce:
        over: children
        total = sum(child.ui.DesiredSize.size.y)
        n = count()
    keep ui.DesiredSize:
        size = vec2(0.0, total + s.gap * (n - 1))

# 6. Typed refs with lifetime policy, and joins through them
trait Bomb:
    owner: ref Player owned       # destroy the owner → destroy the bomb
trait Seeker:
    target: ref Health weak       # target dies → field cleared, `on lost` fires
rule Home:
    filter:
        Seeker as s
        s.target -> tv.WorldTransform as goal    # no row when the target is stale
```

### Genre coverage

```
                  Platf  Shooter  RTS   Sandbox  Turn/Card  UI app/Editor
Today              ●●●    ●●●      ●     ●        ●          ●
+ derive (4, 5)    ●●●    ●●●      ●●    ●●       ●●         ●●●
+ states (2)       ●●●    ●●●      ●●●   ●●       ●●●        ●●●
+ timers (3)       ●●●    ●●●      ●●●   ●●       ●●         ●●
+ relations (6)     —     ●●       ●●●   ●●●      ●●●        ●●●
+ values (8)        —     ●        ●●    ●●●      ●●●        ●●●
Still needed for scale: R1 radius domains, R2 navigation, B1 grid storage
```

A card game and an editor are good stress tests that no example covers yet: they
have no frame-driven simulation, and they need ordered collections, shuffling and
undo.

### Open questions

- Is a derived row entity owned by its source row, so it can't be destroyed by hand?
  Likely yes, under the same single-writer rule as `keep`.
- Does a declared `merge sum` on a field restate a derivable fact? It records game
  intent, not a backend fact, but it needs a decision against "derive, don't restate".
- Copy-on-write collections in traits are safe but can hide O(n) copies. Can the
  backend prove uniqueness and mutate in place?
- Can a game drop the `frame` root and drive phases from events only (turns)? The
  phase model seems to allow it; no example proves it.
- Undo and play-in-editor: command-log transactions need every cross-entity write to
  go through the command buffer (item 7). The world fork can build on persistence
  capture/restore.
