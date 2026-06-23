## MODIFIED Requirements

### Requirement: Keyword recognition
The lexer SHALL recognize all Cactus keywords and produce the corresponding TokenType: `module`, `use`, `const`, `struct`, `enum`, `trait`, `entity`, `system`, `view`, `event`, `func`, `interface`, `let`, `var`, `persist`, `sync`, `pub`, `on`, `emit`, `if`, `else`, `match`, `return`, `filter`, `target`, `map`, `reduce`, `true`, `false`, `as`, `and`, `or`, `not`, `extern`.

The legacy `unit` spelling is no longer part of the active top-level declaration surface; implementations MAY reserve it to provide a targeted migration diagnostic, but authors SHALL use `entity`.

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

#### Scenario: entity keyword tokenized
- **WHEN** the source contains the text `entity Player:`
- **THEN** the lexer produces a token with type ENTITY followed by IDENTIFIER("Player") and COLON

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
