## REMOVED Requirements

### Requirement: Code generation for `enable` statement
**Reason**: `enable` is removed from the DSL.
**Migration**: No migration.

### Requirement: Code generation for `disable` statement
**Reason**: `disable` is removed from the DSL.
**Migration**: No migration.

### Requirement: `initially_active` flag in component initialization
**Reason**: `ApplyEntry.initially_active` is removed; all components in `apply:` are always active at spawn.
**Migration**: No migration.

## ADDED Requirements

### Requirement: Code generation for `add` statement
The backend SHALL generate code for `AddTraitStmt` nodes. For the EnTT backend:
- `add TraitName` on self compiles to `registry.emplace_or_replace<TraitName>(entity, field_values...)`
- `add TraitName(field = val, ...)` compiles to `registry.emplace_or_replace<TraitName>(entity, TraitName{.field = val, ...})`
- `add TraitName to target_expr` compiles to `registry.emplace_or_replace<TraitName>(target_expr, ...)`
- Trait fields not supplied in `add` args that have defaults SHALL use the default value in the generated constructor
- Trait fields not supplied in `add` args that are already present on the component SHALL be patched only for the supplied fields; remaining fields retain their current values via get-then-replace pattern

#### Scenario: add marker trait generates emplace_or_replace
- **WHEN** `add Frozen` is compiled and `Frozen` is a marker trait
- **THEN** the generated code is `registry.emplace_or_replace<Frozen>(entity)`

#### Scenario: add with fields generates struct initialization
- **WHEN** `add Stunned(duration = 2.0)` is compiled and `Stunned` has `var duration: float = 0.0`
- **THEN** the generated code initializes `duration` to `2.0f`

#### Scenario: add to other entity uses target expression
- **WHEN** `add Frozen to other_id` is compiled
- **THEN** the generated code uses `other_id` (resolved from the target expression) as the entity argument

### Requirement: Code generation for `remove` statement
The backend SHALL generate code for `RemoveTraitStmt` nodes. For the EnTT backend:
- `remove TraitName` on self compiles to `registry.remove<TraitName>(entity)`
- `remove TraitName from target_expr` compiles to `registry.remove<TraitName>(target_expr)`
- EnTT's `remove` is a no-op if the component is not present; no guard check is needed

#### Scenario: remove trait generates registry.remove call
- **WHEN** `remove Frozen` is compiled
- **THEN** the generated code is `registry.remove<Frozen>(entity)`

#### Scenario: remove from other entity uses target expression
- **WHEN** `remove Shield from parent_id` is compiled
- **THEN** the generated code is `registry.remove<Shield>(parent_id_value)`

### Requirement: `apply:` block generates unconditional component initialization
With `ApplyEntry.initially_active` removed, all `apply:` entries compile to direct component initialization at entity spawn time. No conditional masks or activation flags are generated.

#### Scenario: apply entry generates emplace at spawn
- **WHEN** a unit declares `apply: Health` with `config: current = 100, max = 100`
- **THEN** the generated spawn code calls `registry.emplace<Health>(entity, 100, 100)` unconditionally
