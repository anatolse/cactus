## MODIFIED Requirements

### Requirement: Keyword recognition
The lexer SHALL recognize all Cactus keywords and produce the corresponding TokenType: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `system`, `view`, `event`, `func`, `interface`, `let`, `var`, `persist`, `sync`, `pub`, `on`, `emit`, `if`, `else`, `match`, `return`, `apply`, `config`, `child`, `filter`, `target`, `map`, `reduce`, `true`, `false`, `as`, `and`, `or`, `not`, `extern`.

#### Scenario: Keyword vs identifier
- **WHEN** the source contains the text `system`
- **THEN** the lexer produces a token with type SYSTEM (not IDENTIFIER)

#### Scenario: Identifier with keyword prefix
- **WHEN** the source contains the text `system_name`
- **THEN** the lexer produces a token with type IDENTIFIER (not SYSTEM)

#### Scenario: extern keyword tokenized
- **WHEN** the source contains the text `extern`
- **THEN** the lexer produces a token with type EXTERN (not IDENTIFIER)

#### Scenario: extern_value not a keyword
- **WHEN** the source contains the text `extern_value`
- **THEN** the lexer produces a token with type IDENTIFIER

## ADDED Requirements

### Requirement: `extern` reserved keyword
The lexer SHALL recognize `extern` as a reserved keyword with token type `EXTERN`. The keyword SHALL NOT be usable as an identifier.

#### Scenario: extern in declaration context tokenized
- **WHEN** the source contains `pub extern func lerp(a, b, t: float) -> float`
- **THEN** the lexer emits `PUB EXTERN FUNC IDENTIFIER("lerp") LPAREN ...`
