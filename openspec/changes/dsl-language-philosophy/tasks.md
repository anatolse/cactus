## 1. Create the Philosophy Spec

- [x] 1.1 Promote `specs/language-philosophy/spec.md` from this change's delta into `openspec/specs/language-philosophy/spec.md` (the permanent main spec location) — the content of this spec is the philosophy document itself

## 2. Update the Showcase

- [x] 2.1 Add a preamble comment to `examples/dsl_showcase.cactus` (after the current section list) noting which sections demonstrate aspirational patterns vs. current implementation gaps — specifically flag `draw_rect` calls as a known stdlib gap, not the intended authoring pattern
- [x] 2.2 Add a reference to the language philosophy in the showcase header: "See openspec/specs/language-philosophy/spec.md for the authoritative language identity and design principles"

## 3. Audit Existing Specs Against the Philosophy

Review each main spec for conflicts with the philosophy. Document conflicts as `# NOTE: conflicts with language-philosophy` comments or as new change proposals. Do NOT modify the specs in this change — only document gaps.

- [x] 3.1 Review `openspec/specs/dsl-type-system/spec.md` — check entity_id section (now updated by `dsl-entity-id-total-semantics`)
- [x] 3.2 Review `openspec/specs/dsl-trait-modifiers/spec.md` — verify enable/disable removal is tracked by `dsl-dynamic-traits`
- [x] 3.3 Review `openspec/specs/dsl-semantic-analysis/spec.md` — verify no imperative constraints are missing (loop rejection, recursion rejection)
- [x] 3.4 Review `openspec/specs/backend-cpp-entt/spec.md` — verify backend concerns (rendering, physics) are backend-owned, not leaked into author surface
- [x] 3.5 Review stdlib specs (`stdlib-render`, `stdlib-physics`, `stdlib-transform`, `stdlib-camera`, `stdlib-audio`) — flag which are "declarative trait → generated behavior" vs. "author writes explicit calls"
- [x] 3.6 Document identified gaps as comments in this tasks.md (append findings below) for future change tracking

---

### Audit Findings (recorded by task 3.6)

#### 3.1 — `openspec/specs/dsl-type-system/spec.md`
**CONFLICT — tracked by `dsl-entity-id-total-semantics`**
- The `entity_id` semantics paragraph says "An `entity_id` value SHALL always refer to a live entity. There is no null, zero, or 'no entity' sentinel value." — directly contradicts the total-semantics model where handles may become stale.
- The scenario "entity_id field always valid at runtime" is incorrect under the new model.
- The rejection error message says "use trait enable/disable to model absent relationships" — references the old model that `dsl-dynamic-traits` removes.
- **Resolution**: `dsl-entity-id-total-semantics` contains delta spec `specs/dsl-type-system/spec.md` that rewrites this section.

#### 3.2 — `openspec/specs/dsl-trait-modifiers/spec.md`
**CONFLICT — tracked by `dsl-dynamic-traits`**
- The entire "`disabled` initial state in `apply:` block" requirement is removed by `dsl-dynamic-traits`.
- The entire "`enable` and `disable` statements" requirement is removed by `dsl-dynamic-traits`.
- The scenario "enable/disable on trait not in apply (invalid)" describes a semantic check that no longer exists under the open-world dynamic model.
- The `exclude:` and filter-optional requirements are COMPATIBLE with the philosophy ✓.
- **Resolution**: `dsl-dynamic-traits` contains delta spec `specs/dsl-trait-modifiers/spec.md` that removes all `enable`/`disable`/`: disabled` requirements.

#### 3.3 — `openspec/specs/dsl-semantic-analysis/spec.md`
**PARTIAL GAP — recursion rejection ✓ present; loop rejection ✗ missing**
- Recursion rejection ("No recursion in func" requirement): ✓ already fully specified with direct + indirect cycle detection.
- Func purity enforcement (no emit, no world access): ✓ already fully specified.
- **GAP**: No requirement exists for rejecting `for`/`while` loop constructs. The philosophy explicitly requires: "loops are not supported; use the system/filter mechanism for iteration". The semantic analysis spec has no such scenario.
- Also note: the error message in `dsl-type-system` for `entity_id == 0` still references `enable`/`disable` — a minor wording gap tracked by `dsl-entity-id-total-semantics`.
- **Resolution for loop gap**: `dsl-loop-rejection` change (task 4.3).

#### 3.4 — `openspec/specs/backend-cpp-entt/spec.md`
**NO CONFLICT — backend concerns correctly owned by backend**
- Rendering (Raylib API), serialization (`persist`), network replication (`sync`), physics, event dispatch, entity creation — all correctly in the backend spec. None leaked to author surface ✓.
- The spec is appropriately scoped: it specifies generated C++ output, not author-facing DSL behavior.
- Missing features (pending changes, not philosophy conflicts):
  - `order by:` clause → tracked by `dsl-system-order-by`
  - `extern system` declaration support → tracked by `dsl-extern-system`
  - Dynamic `add`/`remove` trait operations → tracked by `dsl-dynamic-traits`
  - Trait pattern match (`match entity_id:`) → tracked by `dsl-trait-pattern-match`

#### 3.5 — stdlib specs
**Classification: declarative trait → generated behavior vs. explicit author calls**

| Module | Pattern | Classification | Philosophy alignment |
|---|---|---|---|
| `stdlib-render` | `Renderer`, `AnimatedSprite` traits | Declarative ✓ — "passive traits", backend renders automatically, no user system | ✓ Correct model |
| `stdlib-physics` | `CharacterBody`, `Collider` traits | Declarative ✓ — "physics traits are passive", backend runs simulation in `fixed_tick` | ✓ Correct model |
| `stdlib-transform` | `Transform` trait (flat/volume) | Declarative ✓ — pure field declarations, no behavior authored | ✓ Correct model |
| `stdlib-camera` | `Camera`, `FollowCamera`, `FirstPersonCamera` traits | Declarative ✓ — passive traits, backend camera system (stubs acknowledged in spec) | ✓ Correct model (stubs are known) |
| `stdlib-audio` | `AudioSource`, `MusicTrack`, `AudioSettings` traits | Declarative ✓ — backend reads trait, plays/stops audio automatically | ✓ Correct model |
| `stdlib-audio` | `PlaySound` event | **IMPERATIVE GAP** — author writes `emit PlaySound(...)` explicitly | ✗ Author writing audio API call directly |

The `PlaySound` event is the sole philosophy gap in the stdlib: it is an imperative fire-and-forget command, not a trait-based declarative description. The preferred model would be a `SoundEffect` trait that the backend consumes and plays automatically (one-shot components removed after playback). See task 4.2.

## 4. Future Changes Identified by This Audit

Document known gaps found during the audit. Each will need a separate change proposal:

- [x] 4.1 `dsl-declarative-presentation` — **superseded by `dsl-extern-system`**. The `extern system` mechanism IS the declarative presentation solution: stdlib declares `extern system SpriteRenderer` etc., backend generates optimized rendering. No separate change needed.
- [x] 4.2 `dsl-declarative-audio` — **gap identified**: `std.audio.PlaySound` is an imperative `emit` command (author writes `emit PlaySound(SFX, 0.8, 1.0)` explicitly). The declarative alternative is a one-shot `SoundEffect` component: the author adds a trait to an entity, the backend plays it once and removes the component. This makes audio authoring consistent with the passive-trait model already used by `AudioSource` and `MusicTrack`. Implementation via `extern system AudioPlayer` in `std.audio` — covered by `dsl-extern-system` tasks 6.x when those tasks target the audio stdlib. A new change (`dsl-declarative-audio`) should propose the `SoundEffect` trait and deprecate the `PlaySound` event.
- [x] 4.3 `dsl-loop-rejection` — **gap confirmed**: The semantic analysis spec (`openspec/specs/dsl-semantic-analysis/spec.md`) has no requirement rejecting `for`/`while` constructs. The philosophy requires the compiler to report: "loops are not supported; use the system/filter mechanism for iteration." A new change (`dsl-loop-rejection`) should add this requirement to the semantic analysis spec and implement it in the semantic analyzer. Note: the current parser and AST likely do not even parse loop constructs; the change may be a combined parser + semantic + error message task.
- [x] 4.4 `dsl-recursion-rejection` — **already covered**: The semantic analysis spec already has a complete "No recursion in func" requirement with direct and indirect cycle detection scenarios. No new change needed — the compiler either already enforces this or it is a straightforward implementation task within the existing `dsl-semantic-analysis` spec. Recommend checking `src/frontend/semantic_analyzer.cpp` for the `visit_func_call` recursion check before creating a separate change proposal.
