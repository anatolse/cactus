## Requirements

### Requirement: Indentation-sensitive tokenization
The lexer SHALL maintain an indent stack and emit explicit INDENT and DEDENT tokens when indentation level changes between lines. The lexer SHALL use spaces only (tabs are rejected with an error). Each indentation level increase SHALL produce one INDENT token; each decrease SHALL produce one or more DEDENT tokens to match the target level.

#### Scenario: Nested block produces INDENT/DEDENT
- **WHEN** the source contains a `trait Name:` line followed by an indented field line, then a dedented line
- **THEN** the lexer emits TRAIT, IDENTIFIER, COLON, NEWLINE, INDENT, field tokens, NEWLINE, DEDENT

#### Scenario: Multiple dedent levels
- **WHEN** indentation drops from 8 spaces to 0 spaces (skipping the 4-space level)
- **THEN** the lexer emits two DEDENT tokens

#### Scenario: Tab character rejected
- **WHEN** the source contains a tab character for indentation
- **THEN** the lexer reports an error with the source location

### Requirement: Keyword recognition
The lexer SHALL recognize all Cactus keywords and produce the corresponding TokenType: `module`, `use`, `const`, `struct`, `enum`, `trait`, `entity`, `rule`, `view`, `event`, `func`, `let`, `var`, `persist`, `sync`, `pub`, `on`, `emit`, `if`, `else`, `match`, `return`, `filter`, `target`, `map`, `reduce`, `true`, `false`, `as`, `and`, `or`, `not`, `extern`.

The legacy `unit` spelling is no longer part of the active top-level declaration surface; implementations MAY reserve it to provide a targeted migration diagnostic, but authors SHALL use `entity`. The `system` spelling is no longer a keyword; it lexes as `IDENTIFIER`.

#### Scenario: Keyword vs identifier
- **WHEN** the source contains the text `rule`
- **THEN** the lexer produces a token with type RULE (not IDENTIFIER)

#### Scenario: Identifier with keyword prefix
- **WHEN** the source contains the text `rule_name`
- **THEN** the lexer produces a token with type IDENTIFIER (not RULE)

#### Scenario: extern keyword tokenized
- **WHEN** the source contains the text `extern`
- **THEN** the lexer produces a token with type EXTERN (not IDENTIFIER)

#### Scenario: extern_value not a keyword
- **WHEN** the source contains the text `extern_value`
- **THEN** the lexer produces a token with type IDENTIFIER

#### Scenario: entity keyword tokenized
- **WHEN** the source contains the text `entity Player:`
- **THEN** the lexer produces a token with type ENTITY followed by IDENTIFIER("Player") and COLON

#### Scenario: system used as identifier
- **WHEN** the source contains `system` as a field name or identifier inside a valid declaration
- **THEN** the lexer produces a token with type IDENTIFIER, and the parser accepts it as an ordinary identifier without error

### Requirement: Numeric literal tokenization
The lexer SHALL distinguish integer literals from float literals. A number containing a decimal point SHALL be tokenized as FLOAT_LITERAL; otherwise as INT_LITERAL.

#### Scenario: Integer literal
- **WHEN** the source contains `42`
- **THEN** the lexer produces a token with type INT_LITERAL and value "42"

#### Scenario: Float literal
- **WHEN** the source contains `3.14`
- **THEN** the lexer produces a token with type FLOAT_LITERAL and value "3.14"

### Requirement: String literal tokenization
The lexer SHALL tokenize double-quoted strings as STRING_LITERAL tokens. String literals SHALL only be valid within `const` blocks (enforced by the semantic analyzer, not the lexer).

#### Scenario: String literal
- **WHEN** the source contains `"Hello World"`
- **THEN** the lexer produces a token with type STRING_LITERAL and value `Hello World`

### Requirement: Hex color literal tokenization
The lexer SHALL tokenize `#` followed by 6 or 8 hex digits as HEX_COLOR tokens.

#### Scenario: RGB color
- **WHEN** the source contains `#FF0000`
- **THEN** the lexer produces a token with type HEX_COLOR and value "FF0000"

#### Scenario: RGBA color
- **WHEN** the source contains `#FF000080`
- **THEN** the lexer produces a token with type HEX_COLOR and value "FF000080"

### Requirement: Comment skipping
The lexer SHALL skip single-line comments starting with `#` (when not followed by hex digits forming a color literal) through to end of line, producing no tokens for the comment content.

#### Scenario: Comment line
- **WHEN** the source contains `# this is a comment`
- **THEN** the lexer produces no tokens for that line (only NEWLINE if applicable)

### Requirement: Operator and punctuation tokenization
The lexer SHALL tokenize all operators and punctuation: `:`, `,`, `.`, `->`, `=>`, `(`, `)`, `[`, `]`, `{`, `}`, `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `=`, `+=`, `-=`, `*=`, `/=`.

#### Scenario: Arrow operator
- **WHEN** the source contains `->`
- **THEN** the lexer produces a token with type ARROW

#### Scenario: Fat arrow
- **WHEN** the source contains `=>`
- **THEN** the lexer produces a token with type FAT_ARROW

#### Scenario: Star-assign operator
- **WHEN** the source contains `*=`
- **THEN** the lexer produces a token with type STAR_ASSIGN

#### Scenario: Slash-assign operator
- **WHEN** the source contains `/=`
- **THEN** the lexer produces a token with type SLASH_ASSIGN

### Requirement: New reserved keywords
The lexer SHALL recognize the following additional reserved keywords:

| Keyword | Token |
|---------|-------|
| `template` | `TEMPLATE` |
| `spawn` | `SPAWN` |
| `destroy` | `DESTROY` |
| `load` | `LOAD` |
| `unload` | `UNLOAD` |
| `enable` | `ENABLE` |
| `disable` | `DISABLE` |
| `exclude` | `EXCLUDE` |
| `disabled` | `DISABLED` |
| `to` | `TO` |
| `from` | `FROM` |

These keywords are reserved and SHALL NOT be usable as identifiers.

#### Scenario: template keyword tokenized
- **WHEN** the source contains `template WalkerEnemy:`
- **THEN** the lexer emits `TEMPLATE IDENTIFIER("WalkerEnemy") COLON`

#### Scenario: spawn keyword tokenized
- **WHEN** the source contains `spawn Enemy(pos = vec2(0.0, 0.0))`
- **THEN** the lexer emits `SPAWN IDENTIFIER("Enemy") LPAREN ...`

#### Scenario: destroy keyword tokenized
- **WHEN** the source contains the single token `destroy`
- **THEN** the lexer emits `DESTROY`

#### Scenario: load keyword tokenized
- **WHEN** the source contains `load levels.level1`
- **THEN** the lexer emits `LOAD IDENTIFIER("levels") DOT IDENTIFIER("level1")`

#### Scenario: enable and disable keywords tokenized
- **WHEN** the source contains `enable Frozen` or `disable EnemyAI`
- **THEN** the lexer emits `ENABLE IDENTIFIER(...)` or `DISABLE IDENTIFIER(...)` respectively

#### Scenario: exclude keyword tokenized
- **WHEN** the source contains `exclude:`
- **THEN** the lexer emits `EXCLUDE COLON`

#### Scenario: disabled keyword tokenized in apply block
- **WHEN** the source contains `Frozen: disabled` inside an `apply:` block
- **THEN** the lexer emits `IDENTIFIER("Frozen") COLON DISABLED`

#### Scenario: from keyword tokenized
- **WHEN** the source contains `entity Gem1 from BlueGem:`
- **THEN** the lexer emits `ENTITY IDENTIFIER("Gem1") FROM IDENTIFIER("BlueGem") COLON`

#### Scenario: New keywords rejected as identifiers
- **WHEN** a declaration uses `entity`, `template`, `spawn`, `destroy`, `load`, `enable`, `disable`, `exclude`, `disabled`, `to`, or `from` as an identifier name
- **THEN** the lexer emits the reserved keyword token, and the parser SHALL report an error

### Requirement: `extern` reserved keyword
The lexer SHALL recognize `extern` as a reserved keyword with token type `EXTERN`. The keyword SHALL NOT be usable as an identifier.

#### Scenario: extern in declaration context tokenized
- **WHEN** the source contains `pub extern func lerp(a, b, t: float) float`
- **THEN** the lexer emits `PUB EXTERN FUNC IDENTIFIER("lerp") LPAREN ...`
