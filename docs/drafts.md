# Language review drafts (first-person-arena driven)

Status as of 2026-10-01. Arena: 41 → 32 rules, 1374 → 1267 lines since this review.

| Item | Status |
|---|---|
| A1, A2, A5, A6 | done — simplify-dsl-surface |
| A3 | declined — both entity forms stay |
| A4 | deferred — waits on C6 |
| A7, C3 | done — add-trait-lifecycle-triggers |
| C1 | done — add-deferred-set-command |
| C2 | done — add-named-entity-access |
| C6 | partially done — add-rule-groups (rule-level groups; stages/handler-level deferred) |
| C4, C5, C7, C8, S1–S3, R1–R6, B1–B3 | open |

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

  A4. [DEFERRED — waits on C6 named stages; rule-level after: is no longer called legacy] Keep one ordering mechanism. Right now there are three: rule-level after: (the spec calls it legacy),
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

  C5. Const expressions and const tables
  Today const_value must be a literal, so the arena writes ENEMY_STEER_ANGLE_SMALL = 0.6981317 instead of deg(40.0),
  and HALF_PI = 1.57079633. Two steps:
  1. Pure compile-time expressions in const:.
  2. const structs and lists: const UNITS: list[UnitDef] = [...].

  - RTS: unit stat tables, build costs.
  - Shooter: weapon stats, wave definitions.
  - Sandbox: block properties, crafting recipes, loot tables.
  - This is where game balancing happens. It also pairs with C4.

  C6. Named stages inside a phase
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

  S2. A 3D character controller that actually works (move_and_slide). The stdlib says it simulates this, but the
  flagship 3D game rewrote it in 7 rules with a sentinel value. That gap is the strongest signal in the repo. Build it
  in the stdlib: step height, slopes, jump buffering (add-buffered-character-jumping is in flight), and separation
  between actors.
  - FPS, TPS, 3D platformers, and any sandbox with a walking player.

  S3. Named animation clips and simple animation state. ROBOT_RUN_CLIP = 6 is a magic index with a 10-line comment
  explaining where it comes from. Resolve clips by name at load time: models.clip(Robot, "Robot_Running"). Later, add
  a small AnimState trait with crossfade.

  (Contact Enter/Stay/Exit is already in flight as complete-physics-contact-lifecycle. For bullets it replaces pair
  rules with on CollisionEnter.)

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