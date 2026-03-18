## ADDED Requirements

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
The lexer SHALL recognize all Cactus keywords and produce the corresponding TokenType: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `system`, `view`, `event`, `func`, `interface`, `let`, `var`, `persist`, `sync`, `pub`, `on`, `emit`, `if`, `else`, `match`, `return`, `apply`, `config`, `child`, `filter`, `target`, `map`, `reduce`, `true`, `false`, `as`, `and`, `or`, `not`.

#### Scenario: Keyword vs identifier
- **WHEN** the source contains the text `system`
- **THEN** the lexer produces a token with type SYSTEM (not IDENTIFIER)

#### Scenario: Identifier with keyword prefix
- **WHEN** the source contains the text `system_name`
- **THEN** the lexer produces a token with type IDENTIFIER (not SYSTEM)

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
The lexer SHALL tokenize all operators and punctuation: `:`, `,`, `.`, `->`, `=>`, `(`, `)`, `[`, `]`, `{`, `}`, `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `=`, `+=`, `-=`.

#### Scenario: Arrow operator
- **WHEN** the source contains `->`
- **THEN** the lexer produces a token with type ARROW

#### Scenario: Fat arrow
- **WHEN** the source contains `=>`
- **THEN** the lexer produces a token with type FAT_ARROW
