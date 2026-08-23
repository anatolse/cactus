## ADDED Requirements

### Requirement: Render-pass synthetic pass-local edges

For a render-pass phase, the execution graph SHALL represent one synthetic, non-authored
rasterization node connecting the phase's vertex-stage handler node to its fragment-stage handler
node: vertex-handler node → synthetic rasterization node → fragment-handler node → the
render-pass phase's own node in the ordinary phase DAG. This mirrors the existing representation
of non-authored producer work ("Commit-synthesized and scheduler-boundary producer edges") — the
synthetic node is attributed to backend rasterization, not to any `HandlerIdentity`. No other
phase or handler may depend on the synthetic node directly; only the phase's own node participates
in ordinary `after:` edges to downstream phases.

#### Scenario: Vertex output reaches fragment through the synthetic node

- **WHEN** a render-pass phase has vertex-stage handler `V` and fragment-stage handler `F`
- **THEN** the graph contains edges `V → synthetic rasterization node → F`, and no direct edge
  `V → F`

#### Scenario: Downstream phases depend on the pass, not on either stage handler directly

- **WHEN** a phase declares `after: particle_pass`
- **THEN** its producer edge is to `particle_pass`'s own phase node, unaffected by the internal
  vertex/rasterization/fragment edges

#### Scenario: Stage handlers introduce no new structural-commit interaction

- **WHEN** the graph is validated for a render-pass phase
- **THEN** no structural-command producer/consumer edges are introduced by the stage handlers,
  since `dsl-render-passes`'s body restriction already rejects `spawn`/`destroy`/`add`/`remove`/
  `project` in a stage handler body
