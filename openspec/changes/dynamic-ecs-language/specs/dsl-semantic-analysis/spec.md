## ADDED Requirements

### Requirement: Template declaration validation
The semantic analyzer SHALL validate `template` declarations with the same rules as `unit` declarations: all traits in `apply:` must be declared, all field names in `config:` must belong to applied traits, and `let` fields with no default value are errors. Additionally, the analyzer SHALL track templates as spawnable archetypes for spawn-site validation.

#### Scenario: Template with undeclared trait rejected
- **WHEN** a `template Foo:` lists `apply: UnknownTrait`
- **THEN** the analyzer SHALL report an error: "undeclared trait 'UnknownTrait'"

#### Scenario: Template with invalid config field rejected
- **WHEN** a `template` sets a field in `config:` that belongs to no applied trait
- **THEN** the analyzer SHALL report an error: "unknown field '<name>' in template config"

---

### Requirement: Spawn-site validation
At each `spawn TemplateName(...)` call site, the semantic analyzer SHALL verify:
1. `TemplateName` refers to a known `template` declaration (not a `unit`)
2. All field names in the override list are valid fields of the template's applied traits
3. All required fields (declared `var` with no `config:` default) are either provided in the override list or have a default

#### Scenario: Spawn of undeclared template rejected
- **WHEN** `spawn UnknownFoo()` is used
- **THEN** the analyzer SHALL report an error: "undefined template 'UnknownFoo'"

#### Scenario: Spawn of unit (not template) rejected
- **WHEN** `spawn Player()` is used and `Player` is a `unit`, not a `template`
- **THEN** the analyzer SHALL report an error: "'Player' is a unit, not a template; use `spawn` only with `template` declarations"

#### Scenario: Spawn with missing required field rejected
- **WHEN** a template has a `var` field with no `config:` default and the spawn site omits it
- **THEN** the analyzer SHALL report an error: "required field '<name>' not set for template '<T>'"

#### Scenario: Spawn with unknown override field rejected
- **WHEN** a spawn site provides a field name not in any of the template's applied traits
- **THEN** the analyzer SHALL report an error: "unknown field '<name>' for template '<T>'"

---

### Requirement: `destroy` statement context validation
The semantic analyzer SHALL verify that `destroy` only appears inside a system event handler body (not in `func` bodies, top-level code, or trait bodies).

#### Scenario: Destroy in func body rejected
- **WHEN** `destroy` appears inside a `func` declaration
- **THEN** the analyzer SHALL report an error: "`destroy` only allowed inside system event handlers"

---

### Requirement: `load` statement module reference validation
The semantic analyzer SHALL verify that the module name in a `load` statement is reachable via the module's `use` declarations (direct or transitive). `load` is also restricted to system event handler bodies.

#### Scenario: Load of unreachable module rejected
- **WHEN** `load unknown.module` is used and that module was not imported via `use`
- **THEN** the analyzer SHALL report an error: "unknown module 'unknown.module'; add `use unknown.module` to import it"

#### Scenario: Load in func body rejected
- **WHEN** `load levels.level1` appears inside a `func` declaration
- **THEN** the analyzer SHALL report an error: "`load` only allowed inside system event handlers"

---

### Requirement: `enable`/`disable` trait membership validation
The semantic analyzer SHALL verify that the trait named in an `enable` or `disable` statement is in the `apply:` block of at least one entity archetype (unit or template) that could match the enclosing system's `filter:`. If no matching archetype has the named trait, the analyzer SHALL report an error.

#### Scenario: Enable of trait not in any matching archetype rejected
- **WHEN** a system filtered on `[Position, EnemyAI]` uses `enable Frozen`, but no unit or template with `Position` and `EnemyAI` includes `Frozen` in its `apply:`
- **THEN** the analyzer SHALL report an error: "trait 'Frozen' is not in the apply block of any entity matching this system's filter"

#### Scenario: Disable of trait present in matching archetype accepted
- **WHEN** a template has `Frozen` in its `apply:` and the enclosing system's filter matches that template
- **THEN** `disable Frozen` is accepted without error

---

### Requirement: Lifecycle handler signature validation
The semantic analyzer SHALL verify that `on spawn()`, `on destroy()`, `on load()`, and `on unload()` handlers have empty parameter lists. Providing parameters to these lifecycle handlers is an error.

#### Scenario: on spawn with parameters rejected
- **WHEN** `on spawn(x: float):` appears in a system
- **THEN** the analyzer SHALL report an error: "lifecycle handler 'spawn' does not accept parameters"

#### Scenario: on load with parameters rejected
- **WHEN** `on load(name: string):` appears in a system
- **THEN** the analyzer SHALL report an error: "lifecycle handler 'load' does not accept parameters"

#### Scenario: on unload with parameters rejected
- **WHEN** `on unload(dt: float):` appears in a system
- **THEN** the analyzer SHALL report an error: "lifecycle handler 'unload' does not accept parameters"

---

### Requirement: `exclude:` trait reference validation
The semantic analyzer SHALL verify that all trait names listed in an `exclude:` block are declared traits (data traits or marker traits). Unknown traits in `exclude:` are errors.

#### Scenario: Exclude with undeclared trait rejected
- **WHEN** `exclude: SomeTrait` is used and `SomeTrait` is not declared anywhere
- **THEN** the analyzer SHALL report an error: "undeclared trait 'SomeTrait' in exclude clause"

#### Scenario: Exclude with declared marker trait accepted
- **WHEN** `exclude: Frozen` is used and `trait Frozen` is declared (even as a marker trait)
- **THEN** the exclude clause is valid

---

### Requirement: `disabled` annotation validation in `apply:` block
The semantic analyzer SHALL verify that a `: disabled` annotation on a trait in `apply:` is syntactically valid. Additionally, it SHALL flag a warning (not error) if a trait annotated `: disabled` is also listed in the system's own `filter:` — this is likely a mistake.

#### Scenario: Disabled annotation on marker trait accepted
- **WHEN** a unit has `Frozen: disabled` in its `apply:` and `Frozen` is a declared marker trait
- **THEN** the analyzer accepts the declaration

#### Scenario: Disabled annotation on data trait accepted
- **WHEN** a template has `EnemyAI: disabled` in its `apply:`
- **THEN** the analyzer accepts the declaration; `EnemyAI` fields are still allocated but the trait starts inactive

---

### Requirement: Filter and exclude validation — both optional; no filter matches all

The semantic analyzer SHALL validate that all trait names in `filter:` and `exclude:` blocks are declared traits. Both `filter:` and `exclude:` are optional. A system with no `filter:` block matches all entities (filter_mask = 0). A system may have `exclude:` without `filter:`.

Additionally, the analyzer SHALL enforce that trait field access in a handler body is only valid for traits listed in `filter:`. A system with no `filter:` cannot access trait fields.

#### Scenario: Filter with undeclared trait rejected
- **WHEN** a system's `filter:` lists a trait name that has not been declared
- **THEN** the analyzer SHALL report an error: "undeclared trait '<name>' in filter clause"

#### Scenario: No filter block is valid (match-all)
- **WHEN** a system has no `filter:` block
- **THEN** the analyzer accepts the system as valid; it processes all entities

#### Scenario: Exclude only (no filter) is valid
- **WHEN** a system has `exclude:` but no `filter:`
- **THEN** the analyzer accepts the system; it processes all entities except excluded ones

#### Scenario: Field access without filter rejected
- **WHEN** a system has no `filter:` block but its handler body reads or writes a trait field
- **THEN** the analyzer SHALL report an error: "trait field '<name>' not accessible — no filter clause declares this trait"
