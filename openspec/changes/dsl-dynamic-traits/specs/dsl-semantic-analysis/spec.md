## REMOVED Requirements

### Requirement: `enable`/`disable` statement validation
**Reason**: `enable` and `disable` statements are removed from the DSL.
**Migration**: No migration — these validations are deleted.

### Requirement: `enable`/`disable` only affects current entity
**Reason**: Replaced by `add`/`remove` semantics which support both self and cross-entity targeting.
**Migration**: No migration.

### Requirement: `enable`/`disable` outside event handler rejected
**Reason**: Replaced by `add`/`remove` context validation.
**Migration**: No migration.

### Requirement: `enable`/`disable` on trait not in apply is invalid
**Reason**: With open-world `add`/`remove`, the `apply:` membership check for enable/disable no longer applies.
**Migration**: No migration.

## ADDED Requirements

### Requirement: `add` statement semantic validation
The semantic analyzer SHALL validate `add` statements as follows:
1. The trait name MUST resolve to a declared trait in scope
2. If the named trait has fields with no default values and no existing component, all such fields MUST be supplied as field assignments in the block
3. All supplied field names MUST match declared field names on the trait
4. All supplied field values MUST type-check against the corresponding field's declared type
5. If a `to expr` clause is present, `expr` MUST have type `entity_id`
6. `add` statements MUST only appear inside system event handler bodies

#### Scenario: Valid add with all required fields
- **WHEN** `add Health:` with `current = 100` and `max = 100` appears and both fields are declared without defaults
- **THEN** the semantic analyzer accepts it

#### Scenario: Missing required field on first add
- **WHEN** `add Health` appears and `Health.current: int` has no default
- **THEN** the semantic analyzer SHALL report: "required field 'current' must be supplied in `add Health`"

#### Scenario: Unknown field name
- **WHEN** `add Health:` with `hp = 100` appears and `Health` has no field named `hp`
- **THEN** the semantic analyzer SHALL report: "unknown field 'hp' in `add Health`"

#### Scenario: Field type mismatch
- **WHEN** `add Health:` with `current = true` appears and `Health.current` is type `int`
- **THEN** the semantic analyzer SHALL report a type error

#### Scenario: Cross-entity target type check
- **WHEN** `add Frozen to some_float` appears where `some_float` is of type `float`
- **THEN** the semantic analyzer SHALL report: "`to` target must be of type `entity_id`"

#### Scenario: add in non-handler context rejected
- **WHEN** `add Frozen` appears inside a `func` body
- **THEN** the semantic analyzer SHALL report: "`add` only allowed inside system event handlers"

### Requirement: `remove` statement semantic validation
The semantic analyzer SHALL validate `remove` statements as follows:
1. The trait name MUST resolve to a declared trait in scope
2. If a `from expr` clause is present, `expr` MUST have type `entity_id`
3. `remove` statements MUST only appear inside system event handler bodies

#### Scenario: Valid remove
- **WHEN** `remove Frozen` appears in a system handler and `Frozen` is a declared trait
- **THEN** the semantic analyzer accepts it

#### Scenario: Remove unknown trait
- **WHEN** `remove Phantom` appears and `Phantom` is not declared
- **THEN** the semantic analyzer SHALL report: "undeclared trait 'Phantom'"

#### Scenario: Remove with cross-entity target type check
- **WHEN** `remove Shield from 42` appears (integer literal)
- **THEN** the semantic analyzer SHALL report: "`from` target must be of type `entity_id`"

#### Scenario: remove in non-handler context rejected
- **WHEN** `remove Frozen` appears inside a `func` body
- **THEN** the semantic analyzer SHALL report: "`remove` only allowed inside system event handlers"

### Requirement: Trait field default value validation
The semantic analyzer SHALL validate field default value expressions in trait declarations. The default expression MUST type-check against the field's declared type. Default expressions MUST be constant-foldable (no references to other fields or runtime values).

#### Scenario: Valid default value type
- **WHEN** `var duration: float = 3.0` appears in a trait
- **THEN** the semantic analyzer accepts it

#### Scenario: Default value type mismatch
- **WHEN** `var count: int = 3.14` appears in a trait
- **THEN** the semantic analyzer SHALL report a type error: "default value type 'float' does not match field type 'int'"

#### Scenario: Default value references runtime variable (invalid)
- **WHEN** `var count: int = some_var` appears in a trait where `some_var` is a non-const identifier
- **THEN** the semantic analyzer SHALL report: "trait field default value must be a constant expression"