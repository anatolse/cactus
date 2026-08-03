## Requirements

### Requirement: Runtime trait-set changes use `add` and `remove` as the canonical surface
The active gameplay-core language surface SHALL treat `add` and `remove` as the canonical way to change an entity's trait set at runtime.

Legacy trait toggling forms such as `enable` and `disable` SHALL NOT be part of the active documented gameplay-core syntax.

#### Scenario: Canonical docs use add/remove
- **WHEN** runtime trait mutation is described in the normative language guide or maintained examples
- **THEN** it uses `add` and `remove` rather than legacy toggle syntax

#### Scenario: Legacy toggle syntax receives migration guidance
- **WHEN** older documentation or examples still reference `enable` or `disable`
- **THEN** they are rewritten or flagged with migration guidance toward `add` / `remove`

### Requirement: `add` statement attaches a trait to an entity
The DSL SHALL support an `add` statement inside rule event handlers. `add TraitName` attaches the named trait to the current entity (self). For data traits with fields, `add TraitName:` followed by an indented field-assignment block initializes named fields at attachment time. If the entity already has the trait, existing fields are updated with the supplied values.

#### Scenario: add bare marker trait to self
- **WHEN** `add Frozen` appears in a rule handler and `Frozen` is a declared marker trait
- **THEN** the parser produces an `AddTraitStmt` with `trait_name = "Frozen"`, empty field assignments, and no `target_expr`

#### Scenario: add trait with required fields using block syntax
- **WHEN** the following appears in a handler body:
  ```
  add Health:
      current = 100
      max = 100
  ```
  and `Health` has fields `current: int` and `max: int` with no defaults
- **THEN** the semantic analyzer accepts it and generates code to attach the `Health` component initialized with the supplied values

### Requirement: `remove` statement detaches a trait from an entity
The DSL SHALL support a `remove` statement inside rule event handlers. `remove TraitName` destroys the named trait and all its field data from the current entity. If the entity does not have the trait, `remove` is a no-op at runtime.

#### Scenario: remove trait from self
- **WHEN** `remove Invincible` appears in a rule handler
- **THEN** the parser produces a `RemoveTraitStmt` with `trait_name = "Invincible"` and no `target_expr`

### Requirement: Cross-entity targeting with `to` and `from`
The `add` statement SHALL support an optional `to expr` suffix where `expr` evaluates to `entity_id`, targeting another entity instead of self. The `remove` statement SHALL support an optional `from expr` suffix for the same purpose.

Operations on stale handles remain silent no-ops per total `entity_id` semantics.

#### Scenario: add trait to another entity
- **WHEN** `add Stunned to c.other:` appears where `c.other` is of type `entity_id`
- **THEN** the semantic analyzer accepts it and generates code to attach `Stunned` to `c.other`

#### Scenario: remove trait from another entity
- **WHEN** `remove Frozen from target_id` appears where `target_id` is of type `entity_id`
- **THEN** the semantic analyzer accepts it and removes `Frozen` from that entity at runtime if present

### Requirement: Trait presence models gameplay state transitions in the core profile
The gameplay-core profile SHALL describe state transitions that change an entity's trait set in terms of attaching and removing traits, rather than hidden inactive-state toggles.

#### Scenario: Temporary gameplay state uses added trait
- **WHEN** a gameplay mechanic applies a temporary state such as stun, freeze, or invincibility
- **THEN** the active-language examples model that state through `add TraitName` and `remove TraitName`

#### Scenario: Author learns one runtime trait mutation model
- **WHEN** a new reader learns how runtime trait changes work
- **THEN** they encounter one canonical model based on trait presence and removal instead of parallel toggle-based explanations

### Requirement: Trait field default values
Trait field declarations SHALL support optional default values using `= expr` syntax. Fields with default values MAY be omitted from `add` field blocks. Fields without default values are required when the component is being created for the first time; if the component already exists, omitted fields retain their current values.

#### Scenario: Default value on var field
- **WHEN** `var duration: float = 1.5` appears in a trait declaration
- **THEN** the parser accepts it and stores the default expression in `FieldNode.default_value`

### Requirement: `add` and `remove` are only valid inside rule event handlers
The `add` and `remove` statements SHALL only appear inside rule event handler bodies. Using them outside a rule handler SHALL be a compile-time error.

#### Scenario: add outside event handler is invalid
- **WHEN** `add Frozen` appears inside a `func` body
- **THEN** the semantic analyzer SHALL report an error that `add` is only allowed inside rule event handlers