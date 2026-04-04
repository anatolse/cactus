## Requirements

### Requirement: `add` statement attaches a trait to an entity
The DSL SHALL support an `add` statement inside system event handlers. `add TraitName` attaches the named trait to the current entity (self). For data traits with fields, `add TraitName:` followed by an indented field-assignment block initializes named fields at attachment time. If the entity already has the trait, existing fields are updated with the supplied values (emplace-or-replace semantics). Unsupplied fields with defaults retain their current values; unsupplied fields without defaults retain their current values if the component already exists.

#### Scenario: add bare marker trait to self
- **WHEN** `add Frozen` appears in a system handler and `Frozen` is a declared marker trait (no fields)
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
The DSL SHALL support a `remove` statement inside system event handlers. `remove TraitName` destroys the named trait and all its field data from the current entity (self). If the entity does not have the trait, `remove` is a no-op at runtime. The trait name must resolve to a declared trait; this is validated at compile time.

#### Scenario: remove trait from self
- **WHEN** `remove Invincible` appears in a system handler
- **THEN** the parser produces a `RemoveTraitStmt` with `trait_name = "Invincible"` and no `target_expr`

### Requirement: Cross-entity targeting with `to` and `from`
The `add` statement SHALL support an optional `to expr` suffix where `expr` evaluates to `entity_id`, targeting another entity instead of self. The `remove` statement SHALL support an optional `from expr` suffix for the same purpose. The semantic analyzer SHALL validate that the target expression has type `entity_id`.

Operations on stale handles are silent no-ops per dsl-entity-id-total-semantics.

#### Scenario: add trait to another entity
- **WHEN** `add Stunned to c.other:` appears where `c.other` is of type `entity_id`
- **THEN** the semantic analyzer accepts it and generates code to attach `Stunned` to `c.other`

### Requirement: Trait field default values
Trait field declarations SHALL support optional default values using `= expr` syntax. Fields with default values MAY be omitted from `add` field blocks. Fields without default values are required when the component is being created for the first time; if the component already exists, omitted fields retain their current values.

#### Scenario: Default value on var field
- **WHEN** `var duration: float = 1.5` appears in a trait declaration
- **THEN** the parser accepts it and stores the default expression in `FieldNode.default_value`

### Requirement: `add` and `remove` are only valid inside system event handlers
The `add` and `remove` statements SHALL only appear inside system event handler bodies. Using them outside a system handler (e.g., in a standalone function) SHALL be a compile-time error.

#### Scenario: add outside event handler is invalid
- **WHEN** `add Frozen` appears inside a `func` body
- **THEN** the semantic analyzer SHALL report an error: "`add` only allowed inside system event handlers"