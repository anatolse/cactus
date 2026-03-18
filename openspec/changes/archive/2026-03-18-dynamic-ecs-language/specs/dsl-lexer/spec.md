## ADDED Requirements

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

#### Scenario: New keywords rejected as identifiers
- **WHEN** a declaration uses `template`, `spawn`, `destroy`, `load`, `enable`, `disable`, `exclude`, or `disabled` as an identifier name
- **THEN** the lexer emits the reserved keyword token, and the parser SHALL report an error
