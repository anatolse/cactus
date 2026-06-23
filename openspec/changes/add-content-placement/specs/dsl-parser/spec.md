## MODIFIED Requirements

### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. Supported declarations: `module`, `use`, `const`, `struct`, `enum`, `trait`, `entity`, `system`, `event`, `func`, `extern func`, `template`, `asset`, `input`.

Note: `view`, `interface`, and legacy `unit` are not supported top-level declarations in this language version.

```ebnf
declaration = module_decl | use_decl | const_block | struct_decl
            | enum_decl | trait_decl | entity_decl | template_decl | system_decl
            | event_decl | func_decl | extern_func_decl | asset_decl | input_decl ;
```

#### Scenario: Module and trait declarations
- **WHEN** the source contains a `module` declaration followed by a `trait` declaration
- **THEN** the parser produces a ProgramNode containing a ModuleNode and a TraitNode

#### Scenario: Asset declaration in program
- **WHEN** the source contains `asset PlayerMesh: mesh = "player.glb"` at the top level
- **THEN** the parser produces a ProgramNode containing an `AssetDecl` node

#### Scenario: Input declaration in program
- **WHEN** the source contains an `input Jump: button` declaration at the top level
- **THEN** the parser produces a ProgramNode containing an `InputDecl` node

#### Scenario: Extern func declaration in program
- **WHEN** `pub extern func lerp(a, b, t: float) float` appears at the top level
- **THEN** the parser produces a ProgramNode containing a `FuncNode` with `is_extern = true`

#### Scenario: Entity declaration in program
- **WHEN** the source contains `entity Player:` at the top level
- **THEN** the parser produces a ProgramNode containing an entity declaration node

#### Scenario: Unknown top-level keyword
- **WHEN** the source contains an unrecognized keyword at the top level
- **THEN** the parser reports an error with the source location and expected declaration types

## REMOVED Requirements

### Requirement: Unit parsing with nested archetype entries
**Reason**: The module-level entity instance declaration keyword is renamed from `unit` to `entity`.
**Migration**: Rewrite `[pub] unit Name:` as `[pub] entity Name:`. For load-time instances based on templates, use `entity Name from TemplateName:`.

## MODIFIED Requirements

### Requirement: `template` declaration grammar

The parser SHALL accept `template_decl` as a top-level declaration whose body is a sequence of archetype entries, structurally identical to inline `entity` declarations except using the `TEMPLATE` keyword. Body-level `use TemplateName` entries in this context are template-composition entries, not module imports. `apply:` and `config:` blocks are not part of template syntax.

```ebnf
template_decl = [ "pub" ] "template" IDENTIFIER ":" NEWLINE INDENT
                { archetype_entry }
                DEDENT ;

archetype_entry = template_use_entry | archetype_trait_entry ;
template_use_entry = "use" dotted_name NEWLINE ;
```

#### Scenario: Template with nested trait blocks parsed
- **WHEN** source contains `template Enemy:` followed by `Position:` and indented field assignments
- **THEN** the parser produces a `TemplateDecl` AST node containing a trait entry for `Position`

#### Scenario: Template with pub modifier parsed
- **WHEN** `pub template Foo:` appears in source
- **THEN** the parser produces a `TemplateDecl` node with `is_pub = true`

#### Scenario: Template with template use parsed
- **WHEN** source contains `template WalkerEnemy:` followed by an indented `use EnemyBase` entry
- **THEN** the parser produces a `TemplateDecl` AST node containing an archetype template-use entry for `EnemyBase`

#### Scenario: Qualified archetype template use parsed
- **WHEN** source contains `use enemies.WalkerEnemy` inside a template or entity body
- **THEN** the parser records the used template name as a dotted name for archetype template composition

## ADDED Requirements

### Requirement: Entity parsing with nested archetype entries
The parser SHALL parse `[pub] entity Name:` blocks as a sequence of archetype entries. Each entry is either a body-level `use TemplateName` entry, a bare trait name for a marker trait, or a trait name followed by `:` and an indented field-assignment block. `apply:` and `config:` blocks are not part of entity syntax.

#### Scenario: Entity with nested trait block parsed
- **WHEN** the source contains `entity Cactus:` with `Position:` followed by indented field assignments
- **THEN** the parser produces an entity declaration containing a trait entry for `Position` and its nested assignments

#### Scenario: Entity with marker trait parsed
- **WHEN** the source contains `entity Cactus:` with a bare `Persistent` entry in the body
- **THEN** the parser produces an entity declaration containing a marker-trait entry for `Persistent`

#### Scenario: Entity with template use parsed
- **WHEN** the source contains `entity Walker1:` with an indented `use WalkerEnemy` entry
- **THEN** the parser produces an entity declaration containing an archetype template-use entry for `WalkerEnemy`

#### Scenario: Pub entity parsed
- **WHEN** the source contains `pub entity Player:`
- **THEN** the parser produces an entity declaration with `is_pub = true`

### Requirement: Template-backed entity declarations

The parser SHALL accept top-level `entity` declarations that instantiate a named entity from a template reference with nested trait override entries.

```ebnf
entity_decl = [ "pub" ] "entity" IDENTIFIER [ "from" dotted_name ] ":" NEWLINE INDENT
              { archetype_entry }
              DEDENT ;
```

When the optional `from dotted_name` clause is present, body entries SHALL be interpreted as overrides applied to the referenced template's flattened archetype.

#### Scenario: Template-backed entity from local template parsed
- **WHEN** source contains `entity Gem1 from BlueGem:` followed by nested trait override entries
- **THEN** the parser produces an entity declaration with name `Gem1`, template reference `BlueGem`, and the parsed override entries

#### Scenario: Template-backed entity from qualified template parsed
- **WHEN** source contains `entity Gem1 from items.BlueGem:`
- **THEN** the parser records the template reference as the dotted name `items.BlueGem`

#### Scenario: Template-backed entity from aliased template parsed
- **WHEN** source contains `use items.gems as gems` and `entity Gem1 from gems.BlueGem:`
- **THEN** the parser records the template reference as the dotted name `gems.BlueGem`

#### Scenario: Entity declarations are top-level only
- **WHEN** `entity Gem1 from BlueGem:` appears inside a system handler
- **THEN** the parser reports that entity declarations are only valid at the top level

### Requirement: Legacy unit declarations rejected

The parser SHALL reject legacy `unit` declarations. Authors SHALL use `entity` declarations for module/scene-load entity instances.

#### Scenario: Legacy unit declaration rejected
- **WHEN** source contains `unit Player:` at the top level
- **THEN** the parser reports that `unit` has been renamed to `entity`
