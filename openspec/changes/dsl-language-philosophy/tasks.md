## 1. Create the Philosophy Spec

- [ ] 1.1 Promote `specs/language-philosophy/spec.md` from this change's delta into `openspec/specs/language-philosophy/spec.md` (the permanent main spec location) — the content of this spec is the philosophy document itself

## 2. Update the Showcase

- [ ] 2.1 Add a preamble comment to `examples/dsl_showcase.cactus` (after the current section list) noting which sections demonstrate aspirational patterns vs. current implementation gaps — specifically flag `draw_rect` calls as a known stdlib gap, not the intended authoring pattern
- [ ] 2.2 Add a reference to the language philosophy in the showcase header: "See openspec/specs/language-philosophy/spec.md for the authoritative language identity and design principles"

## 3. Audit Existing Specs Against the Philosophy

Review each main spec for conflicts with the philosophy. Document conflicts as `# NOTE: conflicts with language-philosophy` comments or as new change proposals. Do NOT modify the specs in this change — only document gaps.

- [ ] 3.1 Review `openspec/specs/dsl-type-system/spec.md` — check entity_id section (now updated by `dsl-entity-id-total-semantics`)
- [ ] 3.2 Review `openspec/specs/dsl-trait-modifiers/spec.md` — verify enable/disable removal is tracked by `dsl-dynamic-traits`
- [ ] 3.3 Review `openspec/specs/dsl-semantic-analysis/spec.md` — verify no imperative constraints are missing (loop rejection, recursion rejection)
- [ ] 3.4 Review `openspec/specs/backend-cpp-entt/spec.md` — verify backend concerns (rendering, physics) are backend-owned, not leaked into author surface
- [ ] 3.5 Review stdlib specs (`stdlib-render`, `stdlib-physics`, `stdlib-transform`, `stdlib-camera`, `stdlib-audio`) — flag which are "declarative trait → generated behavior" vs. "author writes explicit calls"
- [ ] 3.6 Document identified gaps as comments in this tasks.md (append findings below) for future change tracking

## 4. Future Changes Identified by This Audit

Document known gaps found during the audit. Each will need a separate change proposal:

- [ ] 4.1 `dsl-declarative-presentation` — replace explicit `draw_rect`/`draw_sprite` calls in authored systems with stdlib Sprite/Mesh traits + generated render systems
- [ ] 4.2 `dsl-declarative-audio` — replace explicit audio playback calls with AudioTrack trait + generated backend
- [ ] 4.3 `dsl-loop-rejection` — if the compiler does not already reject `for`/`while`, add that validation to the semantic analyzer
- [ ] 4.4 `dsl-recursion-rejection` — if the compiler does not already reject `func` self-calls, add that validation
