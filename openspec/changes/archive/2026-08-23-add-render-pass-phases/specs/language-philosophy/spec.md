## ADDED Requirements

### Requirement: Device and execution-target placement is a backend decision, never authored

Device and execution-target placement — which backend, code path, or execution unit realizes a
declared construct — SHALL always be derived by the compiler/backend from the construct's declared
data, used operations, and the selected backend's capabilities. It SHALL NEVER be expressed as an
author-written marker, keyword, or annotation on any declaration (a `gpu`, `shader`, `target`, or
`kind`-style clause is explicitly disallowed for this purpose, whether on a `phase`, `rule`, or any
other declaration). This requirement binds future placement-related language work as well as this
change's own render-pass mechanism (`dsl-render-passes`), which has only one lowering path today
and therefore makes no placement choice yet — the requirement exists so a later change that does
introduce a real choice does not introduce such a marker to express it.

#### Scenario: A render-pass phase carries no device marker

- **WHEN** a `phase` is recognized as a render-pass phase (`dsl-render-passes`)
- **THEN** its declaration contains no keyword or field naming a device, target, or execution kind
  — only the `Pass`/`Target` descriptor fields, which name a rendering *pipeline shape*, not a
  device

#### Scenario: A future placement-choice proposal is evaluated against this requirement

- **WHEN** a future change proposes letting the backend choose between two or more lowering
  targets for the same construct (e.g. CPU vs. an additional GPU compute path for an ordinary
  rule)
- **THEN** the proposal SHALL be rejected if it expresses that choice as an author-written marker,
  regardless of how the eligibility analysis itself is designed
