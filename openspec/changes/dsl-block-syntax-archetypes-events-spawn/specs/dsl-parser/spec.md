## MODIFIED Requirements

### Requirement: Unit parsing with apply and config blocks
The parser SHALL parse `[pub] unit Name:` blocks as a sequence of nested trait entries. Each entry is either a bare trait name for a marker trait or a trait name followed by `:` and an indented field-assignment block. `apply:` and `config:` blocks are not part of unit syntax.

#### Scenario: Unit with nested trait block parsed
- **WHEN** the source contains `unit Cactus:` with `Position:` followed by indented field assignments
- **THEN** the parser produces a UnitNode containing a trait entry for `Position` and its nested assignments

#### Scenario: Unit with marker trait parsed
- **WHEN** the source contains `unit Cactus:` with a bare `Persistent` entry in the body
- **THEN** the parser produces a UnitNode containing a marker-trait entry for `Persistent`

### Requirement: `template` declaration grammar
The parser SHALL accept `template_decl` as a top-level declaration whose body is a sequence of nested trait entries, structurally identical to `unit_decl` except using the `TEMPLATE` keyword. `apply:` and `config:` blocks are not part of template syntax.

```ebnf
template_decl = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                { archetype_trait_entry }
                DEDENT ;
```

#### Scenario: Template with nested trait blocks parsed
- **WHEN** source contains `template Enemy:` followed by `Position:` and indented field assignments
- **THEN** the parser produces a `TemplateDecl` AST node containing a trait entry for `Position`

#### Scenario: Template with pub modifier parsed
- **WHEN** `pub template Foo:` appears in source
- **THEN** the parser produces a `TemplateDecl` node with `is_pub = true`

### Requirement: `spawn` as primary expression returning `entity_id`
The parser SHALL accept `spawn` as a primary expression and statement using block syntax instead of parenthesized flat arguments.

```ebnf
spawn_expr = "spawn" IDENTIFIER [ "to" expression ]? ":" NEWLINE INDENT
             { archetype_trait_entry }
             DEDENT ;
```

#### Scenario: Spawn expression assigned to let binding
- **WHEN** `let enemy = spawn Enemy:` appears with an indented `Position:` override block
- **THEN** the parser produces a `LetDecl` with a `SpawnExpr` initializer carrying nested override entries

#### Scenario: Spawn statement discard result valid
- **WHEN** `spawn Enemy:` appears as a standalone statement with an indented override block
- **THEN** the parser produces a statement wrapping a `SpawnExpr`

### Requirement: `emit` statement grammar
The parser SHALL accept `emit` as a statement using block syntax for payload initialization and an optional `to expression` suffix before the colon for targeted dispatch.

```ebnf
emit_stmt = "emit" IDENTIFIER [ "to" expression ] ":" NEWLINE INDENT
            { IDENTIFIER "=" expression NEWLINE }
            DEDENT ;
```

#### Scenario: Broadcast emit block parsed
- **WHEN** `emit PlayerDamaged:` appears with indented payload field assignments
- **THEN** the parser produces an `EmitStmt` with `event_name = "PlayerDamaged"`, no target, and the parsed payload assignments

#### Scenario: Targeted emit block parsed
- **WHEN** `emit Damage to EnemyAI.target:` appears with indented payload field assignments
- **THEN** the parser produces an `EmitStmt` with a target expression and the parsed payload assignments

## REMOVED Requirements

### Requirement: Optional `as` alias in `apply:` entries of units and templates
**Reason**: Archetype declarations no longer use `apply:` entries. Trait ownership is expressed structurally through nested trait blocks, so archetype-local aliases are unnecessary.
**Migration**: Replace `apply:` entries and alias-qualified config keys with nested trait blocks under the trait name.

### Requirement: Dotted key form in `config:` assignments and `spawn` override arguments
**Reason**: `config:` blocks and flat parenthesized spawn override arguments are removed. Nested trait blocks make field ownership explicit without dotted keys.
**Migration**: Move each field assignment under its owning trait block in the unit/template or spawn body.