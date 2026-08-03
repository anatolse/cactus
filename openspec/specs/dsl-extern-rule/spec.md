# dsl-extern-rule Specification

## Purpose
TBD - created by archiving change rename-system-to-rule. Update Purpose after archive.
## Requirements
### Requirement: `extern rule` top-level declaration
The DSL SHALL support `extern rule Name:` as a container for backend- or user-library-implemented handlers. It MAY declare `filter:`, `exclude:`, and `order by:` selection clauses and MUST declare at least one `on Trigger:` external handler with an explicit contract. Selectionless external rules SHALL be valid.

#### Scenario: External rule with contracted handler is valid
- **WHEN** NativeMovement declares a filter and an `on fixed_tick` handler with reads, writes, and emits clauses
- **THEN** parsing and semantic analysis accept the external handler contract

#### Scenario: External producer has no filter
- **WHEN** InputSource declares selectionless `on input` with `emits: InputSample`
- **THEN** it is accepted and runs once per input activation

#### Scenario: External rule without handlers is rejected
- **WHEN** an `extern rule` contains selection clauses but no `on` handler
- **THEN** semantic analysis reports that at least one external handler contract is required

### Requirement: Stdlib extern rules run automatically from module import
When a module imports a stdlib module that declares `extern rule` declarations (e.g., `use std.render.sprites`), those extern rules SHALL be automatically included in the program's rule schedule. Authors do NOT need to re-declare them; applying the relevant traits to entities is sufficient.

#### Scenario: SpriteRenderer runs automatically
- **WHEN** a program imports `std.render.sprites` and applies `std.render.sprites.Renderer` to an entity
- **THEN** `SpriteRenderer` runs each frame without any additional author declaration

#### Scenario: Unused extern rule is not included
- **WHEN** a program imports `std.render.sprites` but no entity has `std.render.sprites.Renderer` applied
- **THEN** `SpriteRenderer` is not included in the generated rule schedule (the filter matches zero entities; the backend MAY omit the rule entirely as an optimization)

### Requirement: User-defined extern rules generate typed C++ scaffold
For every user-defined external handler, the backend SHALL generate a typed user-library callback contract derived from that handler's trigger, selection, reads, writes, commands, emitted events, and effects. Callback naming SHALL distinguish handlers of the same rule.

#### Scenario: Per-handler callback is generated
- **WHEN** MyParticleRule declares `on fixed_tick` and `on ParticleReset`
- **THEN** the generated user API contains distinct typed callbacks for both handlers

#### Scenario: Missing callback remains a link error
- **WHEN** a required user external-handler implementation is absent
- **THEN** final linking reports the missing canonical handler callback
