## MODIFIED Requirements

### Requirement: Trait declaration body restricted to field declarations only
The parser SHALL accept only `field_decl` entries inside a trait body. Event handlers (`on ...`) and `func` declarations are no longer valid inside a trait body. Encountering an `on` keyword or `func` keyword inside a trait block SHALL produce a parse error with a helpful message directing the developer to declare a system instead.

The updated grammar for `trait_decl`:

```ebnf
trait_decl      = [ "pub" ] "trait" IDENTIFIER
                  [ ":" NEWLINE INDENT
                    { field_decl }
                    DEDENT ] ;
```

#### Scenario: Trait with only field declarations accepted
- **WHEN** a trait body contains only `let` and `var` field declarations
- **THEN** the parser accepts the trait body

#### Scenario: Marker trait (no body) accepted
- **WHEN** a `trait Frozen` appears with no colon and no body
- **THEN** the parser accepts it as a valid marker trait

#### Scenario: Event handler inside trait body rejected
- **WHEN** a trait body contains `on tick(dt: float):`
- **THEN** the parser reports an error: "event handlers are not allowed in trait bodies; declare a system instead"

#### Scenario: Func inside trait body rejected
- **WHEN** a trait body contains `func helper() float:`
- **THEN** the parser reports an error: "func declarations are not allowed in trait bodies; use a top-level func instead"

## ADDED Requirements

### Requirement: `after:` clause parsing in system declarations
The parser SHALL parse an optional `after:` clause inside a system body. The `after:` clause uses the same indented block structure as `filter:` and `exclude:`: `AFTER COLON NEWLINE INDENT { IDENTIFIER NEWLINE } DEDENT`. The `after:` clause MUST appear after any `filter:` and `exclude:` blocks and before the first event handler.

```ebnf
system_decl     = "system" IDENTIFIER ":" NEWLINE INDENT
                  [ filter_clause ]
                  [ exclude_clause ]
                  [ after_clause ]
                  { event_handler }
                  DEDENT ;
after_clause    = "after" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
```

The keyword `after` SHALL be added to the lexer keyword set with token type `AFTER`.

#### Scenario: `after:` with single entry parsed correctly
- **WHEN** a system body contains an `after:` block with one indented system name
- **THEN** the parser populates `SystemNode.after_systems` with that one name

#### Scenario: `after:` with multiple entries parsed correctly
- **WHEN** a system body contains an `after:` block with `SystemA` and `SystemB` on separate lines
- **THEN** the parser populates `SystemNode.after_systems` with `["SystemA", "SystemB"]`

#### Scenario: Empty `after:` block is a parse error
- **WHEN** a system body contains `after:` with an empty indented block
- **THEN** the parser reports an error: "after: block must contain at least one system name"

#### Scenario: `after:` must appear before event handlers
- **WHEN** an `after:` block appears after an `on tick():` handler
- **THEN** the parser reports an error: "'after:' clause must appear before event handlers"

## ADDED Requirements (config/spawn qualification)

### Requirement: Optional `as` alias in `apply:` entries of units and templates
The parser SHALL accept an optional `as IDENTIFIER` alias after the trait name (dotted_name) in each `apply:` block entry, before any `: disabled` annotation.

```ebnf
apply_entry     = dotted_name [ "as" IDENTIFIER ] [ ":" "disabled" ] NEWLINE ;
```

#### Scenario: Apply entry with alias and disabled both parsed
- **WHEN** `apply:` contains `EnemyAI as ai: disabled`
- **THEN** the parser records `alias = "ai"` and `initially_active = false`

#### Scenario: Apply entry with alias only parsed
- **WHEN** `apply:` contains `Position as pos`
- **THEN** the parser records `alias = "pos"` and `initially_active = true`

### Requirement: Dotted key form in `config:` assignments and `spawn` override arguments
The parser SHALL accept a `config_key` that is either a bare `IDENTIFIER` or a dotted `IDENTIFIER.IDENTIFIER` (two identifiers separated by a `.` with no whitespace on the same logical line). The `config_assign` grammar is updated:

```ebnf
config_assign   = config_key "=" expression NEWLINE ;
config_key      = IDENTIFIER [ "." IDENTIFIER ] ;
```

The same `config_key` grammar applies to `spawn` override argument names:

```ebnf
spawn_arg       = config_key "=" expression ;
```

#### Scenario: Bare config key parsed
- **WHEN** `config:` contains `health = 100`
- **THEN** `ConfigAssignment.key` is `("health", none)` (bare form)

#### Scenario: Dotted config key parsed
- **WHEN** `config:` contains `Health.health = 100`
- **THEN** `ConfigAssignment.key` is `("Health", "health")` (dotted form)

#### Scenario: Dotted spawn override key parsed
- **WHEN** `spawn Enemy(EnemyAI.patrol_speed = 5.0)` is parsed
- **THEN** the spawn arg key is `("EnemyAI", "patrol_speed")`
