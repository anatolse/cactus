## REMOVED Requirements

### Requirement: `enable` statement parsing
**Reason**: `enable` is replaced by `add`. No backward compatibility.
**Migration**: Replace `enable TraitName` with `add TraitName` (or `add MarkerTrait` for the filter-exclusion pattern).

### Requirement: `disable` statement parsing
**Reason**: `disable` is replaced by `remove` (via marker traits). No backward compatibility.
**Migration**: Replace `disable TraitName` patterns with `add MarkerTrait` / `remove MarkerTrait` combined with `exclude:` on systems.

### Requirement: `: disabled` annotation in `apply:` block
**Reason**: `apply:` becomes a plain trait list. No initial-state annotations.
**Migration**: Remove `: disabled` from all `apply:` entries.

## ADDED Requirements

### Requirement: `add` statement parsing
The parser SHALL recognize `add IDENTIFIER` as a statement inside event handler bodies. The `add` statement has two forms: bare (for markers and all-defaulted traits) and block (for data traits with field assignments). An optional `to expr` clause may appear after the trait name to specify a target entity. The `add` and `to` keywords SHALL be added to the keyword list.

```ebnf
add_stmt = "add" IDENTIFIER ["to" expr] NEWLINE
         | "add" IDENTIFIER ["to" expr] ":" NEWLINE INDENT
           { IDENTIFIER "=" expr NEWLINE }
           DEDENT ;
```

#### Scenario: bare add statement parsed
- **WHEN** `add Frozen` appears in a handler body
- **THEN** the parser produces an `AddTraitStmt` with `trait_name = "Frozen"`, empty field assignments, no `target_expr`

#### Scenario: add with block field assignments parsed
- **WHEN** the following appears in a handler body:
  ```
  add Health:
      current = 100
      max = 100
  ```
- **THEN** the parser produces an `AddTraitStmt` with `trait_name = "Health"` and two field assignments

#### Scenario: add with target entity and block parsed
- **WHEN** the following appears in a handler body:
  ```
  add Stunned to other_id:
      duration = 2.0
  ```
- **THEN** the parser produces an `AddTraitStmt` with `trait_name = "Stunned"`, one field assignment, and `target_expr = IdentExpr("other_id")`

### Requirement: `remove` statement parsing
The parser SHALL recognize `remove IDENTIFIER` as a statement inside event handler bodies. An optional `from expr` clause may follow to specify a target entity. The `remove` and `from` keywords SHALL be added to the keyword list.

```ebnf
remove_stmt = "remove" IDENTIFIER ["from" expr] NEWLINE ;
```

#### Scenario: bare remove statement parsed
- **WHEN** `remove Frozen` appears in a handler body
- **THEN** the parser produces a `RemoveTraitStmt` with `trait_name = "Frozen"` and no `target_expr`

#### Scenario: remove with target entity parsed
- **WHEN** `remove Shield from parent_id` appears in a handler body
- **THEN** the parser produces a `RemoveTraitStmt` with `trait_name = "Shield"` and `target_expr = IdentExpr("parent_id")`

### Requirement: Trait field default value parsing
The parser SHALL accept an optional `= expr` initializer after the type in a field declaration within a trait body. This populates `FieldNode.default_value`.

```ebnf
field_decl = field_modifiers IDENTIFIER ":" type_ref ["=" expr] ;
```

#### Scenario: field with default value parsed
- **WHEN** `var duration: float = 3.0` appears in a trait body
- **THEN** the parser produces a `FieldNode` with `default_value = LiteralExpr(3.0)`

#### Scenario: field without default value parsed normally
- **WHEN** `var health: int` appears in a trait body
- **THEN** the parser produces a `FieldNode` with `default_value = nullopt`