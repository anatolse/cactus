# dsl-render-passes Specification

## Purpose

Define the render-pass phase mechanism: a `phase` becomes a render pass when a field resolves to
the canonical stdlib type `std.render.passes.Pass`, and implicitly exposes derived `<phase>.vertex`
and `<phase>.fragment` stage triggers with cardinality and body restrictions enforced at semantic
analysis. No new grammar production, keyword, or reserved word is introduced; recognition is
entirely by resolved field type identity, and `on <phase>.vertex:`/`on <phase>.fragment:` already
parse today (`event_name = dotted_name`, spec §3.9) — only their resolution is new.

## Requirements

### Requirement: Render-pass phase recognition by descriptor field type

A `phase` declaration SHALL be recognized as a render-pass phase when at least one of its
`phase_field_decl` entries has a resolved canonical type of `std.render.passes.Pass`. The field's
declared name SHALL NOT be considered; only its resolved type. Recognition SHALL be by resolved
canonical type identity, so an aliased import of `std.render.passes` is recognized identically to an
unaliased one. A render-pass phase SHALL also declare exactly one field resolving to
`std.render.passes.Target`; a phase with a `Pass` field and no `Target` field SHALL be diagnosed as
incomplete. Both fields' initializing expressions MUST be compile-time constant; a non-constant
`Pass`/`Target` field expression SHALL be diagnosed at that field.

#### Scenario: Phase with a Pass field is recognized regardless of field name

- **WHEN** a `phase` declares a field `pipeline: passes.Pass = passes.Pass.Quads` (or any other
  field name resolving to the same type)
- **THEN** the compiler recognizes the phase as a render-pass phase

#### Scenario: Ordinary phase is unaffected

- **WHEN** a `phase` declares only fields of ordinary types (e.g. `alpha: float =
  fixed_tick.alpha`)
- **THEN** the compiler does not recognize it as a render-pass phase and no derived stage triggers
  are exposed for it

#### Scenario: Missing Target field is diagnosed

- **WHEN** a `phase` declares a `Pass`-typed field and no `Target`-typed field
- **THEN** compilation fails with a diagnostic naming the phase and the missing `Target` descriptor

#### Scenario: Non-constant descriptor expression is diagnosed at the field

- **WHEN** a `Pass`- or `Target`-typed phase field is initialized with an expression that is not
  compile-time constant
- **THEN** compilation fails with a diagnostic naming that field, not any later stage handler

### Requirement: Derived vertex and fragment stage triggers

A render-pass phase SHALL implicitly expose two derived triggers, `<phase>.vertex` and
`<phase>.fragment`, addressable through the existing `on <dotted-name>:`/`on <dotted-name> as
<alias>:` handler syntax. These triggers SHALL NOT be available on a phase that is not recognized
as a render-pass phase, and referencing `<non-render-phase>.vertex` or `.fragment` SHALL be
diagnosed as an unresolvable trigger.

#### Scenario: Vertex and fragment triggers resolve on a render-pass phase

- **WHEN** `particle_pass` is a recognized render-pass phase
- **THEN** `on particle_pass.vertex as v:` and `on particle_pass.fragment as f:` resolve to the
  phase's derived stage triggers

#### Scenario: Stage suffixes are rejected on a non-render phase

- **WHEN** a rule declares `on tick.vertex:` where `tick` is not a render-pass phase
- **THEN** compilation fails with a diagnostic stating that `tick` exposes no such stage

### Requirement: Exactly one vertex and one fragment handler per render-pass phase

A render-pass phase MUST have exactly one handler targeting its `<phase>.vertex` trigger and
exactly one handler targeting its `<phase>.fragment` trigger for the program to compile.

#### Scenario: Missing stage handler is diagnosed

- **WHEN** a render-pass phase has a vertex-stage handler but no fragment-stage handler
- **THEN** compilation fails with a diagnostic naming the phase and the missing stage

#### Scenario: Duplicate stage handler is diagnosed

- **WHEN** two rules each declare a handler targeting the same `<phase>.vertex` trigger
- **THEN** compilation fails with a diagnostic naming both handlers' canonical identities

### Requirement: Vertex handler is unary; fragment handler is selectionless

The handler targeting `<phase>.vertex` MUST have a unary domain (`filter:` selects the
per-instance entity domain; `exclude:`/`order by:`/`where:` remain legal exactly as on an ordinary
rule; `pairs:` is rejected). The handler targeting `<phase>.fragment` MUST be selectionless: it
MUST NOT declare `filter:`, `exclude:`, `pairs:`, or `where:`, and MUST NOT read any durable ECS
trait.

#### Scenario: Vertex handler without filter is diagnosed

- **WHEN** a handler targeting `<phase>.vertex` declares no `filter:`
- **THEN** compilation fails with a diagnostic stating the vertex handler requires an instance
  domain

#### Scenario: Fragment handler with a filter is diagnosed

- **WHEN** a handler targeting `<phase>.fragment` declares a `filter:` block
- **THEN** compilation fails with a diagnostic stating the fragment stage is selectionless

### Requirement: Stage handler body is restricted to a fixed, GLSL-translatable statement subset

A stage handler body (vertex or fragment) MUST be limited to: `let`/`var` bindings that are
invocation-local and never persisted; assignment or compound assignment to invocation-local
variables and to the handler's own writable built-in stage-output fields
(`std.render.passes`'s `Quads` field table); `if`/`else`; and calls to `func` or to an `extern
func` registered as having a portable GLSL translation. `spawn`, `destroy`, `add`, `remove`,
`project`, `emit`, world queries, bounded `for`, and reads of any durable ECS trait from a fragment
handler MUST be rejected during semantic analysis with a diagnostic naming the offending statement
and this restriction.

#### Scenario: Forbidden statement in a stage handler is diagnosed

- **WHEN** a vertex- or fragment-stage handler body contains a `spawn`, `destroy`, `emit`, or
  bounded `for` statement
- **THEN** compilation fails with a diagnostic naming that statement and the stage-handler
  restriction it violates

#### Scenario: Vertex handler may read its filtered traits but not write them

- **WHEN** a vertex-stage handler reads a trait field bound by its own `filter:` (e.g.
  `xf.position`)
- **THEN** the read is accepted
- **WHEN** the same handler attempts to assign to that trait field
- **THEN** compilation fails with a diagnostic stating only built-in stage-output fields are
  writable

#### Scenario: Extern func without a registered GLSL translation is rejected

- **WHEN** a stage handler body calls an `extern func` that has no registered portable GLSL
  translation
- **THEN** compilation fails with a diagnostic naming the call and stating it has no portable
  translation for this backend
