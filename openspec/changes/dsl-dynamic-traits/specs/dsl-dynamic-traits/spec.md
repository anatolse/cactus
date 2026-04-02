## ADDED Requirements

### Requirement: `add` statement attaches a trait to an entity
The DSL SHALL support an `add` statement inside system event handlers. `add TraitName` attaches the named trait to the current entity (self). `add TraitName(field = expr, ...)` initializes named fields at attachment time. If the entity already has the trait, existing fields are updated with the supplied values (emplace-or-replace semantics). Unsupplied fields with defaults retain their current values; unsupplied fields without defaults retain their current values if the component already exists.

#### Scenario: add bare marker trait to self
- **WHEN** `add Frozen` appears in a system handler and `Frozen` is a declared marker trait (no fields)
- **THEN** the parser produces an `AddTraitStmt` with `trait_name = "Frozen"`, empty `args`, and no `target_expr`

#### Scenario: add trait with required fields
- **WHEN** `add Health(current = 100, max = 100)` appears and `Health` has fields `current: int` and `max: int` with no defaults
- **THEN** the semantic analyzer accepts it and generates code to attach the `Health` component initialized with the supplied values

#### Scenario: add trait with partial args when fields have defaults
- **WHEN** `add Invincible(duration = 2.0)` appears and `Invincible` has `var duration: float = 1.5`
- **THEN** the semantic analyzer accepts it; `duration` is set to `2.0`

#### Scenario: add trait with no args when all fields have defaults
- **WHEN** `add Invincible` appears and all fields of `Invincible` have default values
- **THEN** the semantic analyzer accepts it; all fields use their default values on first add

#### Scenario: add trait with missing required field is an error
- **WHEN** `add Health` appears and `Health` has `var current: int` (no default) and `var max: int` (no default)
- **THEN** the semantic analyzer SHALL report an error: "required field 'current' must be supplied in `add Health`"

#### Scenario: add with unknown trait name is an error
- **WHEN** `add UnknownTrait` appears in source
- **THEN** the semantic analyzer SHALL report an error: "undeclared trait 'UnknownTrait'"

#### Scenario: add idempotent — re-adding existing trait patches fields
- **WHEN** an entity already has `Invincible(duration = 0.5)` and `add Invincible(duration = 2.0)` executes
- **THEN** the entity's `Invincible.duration` becomes `2.0`; the entity still has `Invincible`

### Requirement: `remove` statement detaches a trait from an entity
The DSL SHALL support a `remove` statement inside system event handlers. `remove TraitName` destroys the named trait and all its field data from the current entity (self). If the entity does not have the trait, `remove` is a no-op at runtime. The trait name must resolve to a declared trait; this is validated at compile time.

#### Scenario: remove trait from self
- **WHEN** `remove Invincible` appears in a system handler
- **THEN** the parser produces a `RemoveTraitStmt` with `trait_name = "Invincible"` and no `target_expr`

#### Scenario: remove destroys field data
- **WHEN** `remove Health` executes on an entity that has `Health`
- **THEN** the `Health` component and all its field values are destroyed; the entity no longer has `Health`

#### Scenario: remove on absent trait is a no-op
- **WHEN** `remove Frozen` executes on an entity that does not have `Frozen`
- **THEN** no error occurs; execution continues normally

#### Scenario: remove with unknown trait name is an error
- **WHEN** `remove UnknownTrait` appears in source
- **THEN** the semantic analyzer SHALL report an error: "undeclared trait 'UnknownTrait'"

### Requirement: Cross-entity targeting with `to` and `from`
The `add` statement SHALL support an optional `to expr` clause where `expr` evaluates to `entity_id`, targeting another entity instead of self. The `remove` statement SHALL support an optional `from expr` clause for the same purpose. The semantic analyzer SHALL validate that the target expression has type `entity_id`.

#### Scenario: add trait to another entity
- **WHEN** `add Stunned(duration = 3.0) to c.other` appears where `c.other` is of type `entity_id`
- **THEN** the semantic analyzer accepts it and generates code to attach `Stunned` to `c.other`

#### Scenario: remove trait from another entity
- **WHEN** `remove Collectible from c.other` appears where `c.other` is of type `entity_id`
- **THEN** the semantic analyzer accepts it and generates code to detach `Collectible` from `c.other`

#### Scenario: cross-entity target must be entity_id
- **WHEN** `add Frozen to 42` appears (integer literal, not entity_id)
- **THEN** the semantic analyzer SHALL report a type error: "`to` target must be of type `entity_id`"

### Requirement: Trait field default values
Trait field declarations SHALL support optional default values using `= expr` syntax. Fields with default values MAY be omitted in `add` argument lists. Fields without default values are required in `add` argument lists when the component is being created for the first time; if the component already exists, omitted fields retain their current values.

#### Scenario: Default value on var field
- **WHEN** `var duration: float = 1.5` appears in a trait declaration
- **THEN** the parser accepts it and stores the default expression in `FieldNode.default_value`

#### Scenario: Default value on let field
- **WHEN** `let max_health: int = 100` appears in a trait declaration
- **THEN** the parser accepts it; the default is used when no value is supplied in `add`

#### Scenario: add with omitted defaulted field uses default
- **WHEN** `add Invincible` and `Invincible` has `var duration: float = 1.5`
- **THEN** the component is created with `duration = 1.5`

### Requirement: `add` and `remove` are only valid inside system event handlers
The `add` and `remove` statements SHALL only appear inside system event handler bodies. Using them outside a system handler (e.g., in a standalone function) SHALL be a compile-time error.

#### Scenario: add inside event handler is valid
- **WHEN** `add Frozen` appears inside `on tick:` of a system
- **THEN** the semantic analyzer accepts it

#### Scenario: add outside event handler is invalid
- **WHEN** `add Frozen` appears inside a `func` body
- **THEN** the semantic analyzer SHALL report an error: "`add` only allowed inside system event handlers"
