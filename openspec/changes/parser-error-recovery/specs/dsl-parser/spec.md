## ADDED Requirements

### Requirement: Parser SHALL guarantee forward progress on all inputs
The parser MUST guarantee forward progress through the token stream, even when encountering syntax errors. The parser SHALL NOT enter infinite loops or hang indefinitely on any input, including malformed programs.

#### Scenario: Parser completes on malformed input without hanging
- **WHEN** the parser encounters a syntax error in any parsing loop
- **THEN** the parser SHALL advance past the error and continue parsing or terminate within bounded time

#### Scenario: Parser with unexpected token in struct field list
- **WHEN** parsing a struct body encounters an unexpected token (e.g., `garbage` instead of field name)
- **THEN** the parser SHALL report the error and skip to the next valid synchronization point without infinite looping

#### Scenario: Parser with malformed trait body
- **WHEN** parsing a trait body with missing colons or invalid syntax
- **THEN** the parser SHALL report errors and complete parsing within bounded time

#### Scenario: Parser with deeply nested malformed blocks
- **WHEN** parsing deeply nested blocks with syntax errors at multiple levels
- **THEN** the parser SHALL recover at each level and complete without memory exhaustion

### Requirement: Parser SHALL implement panic-mode error recovery
The parser MUST implement panic-mode synchronization that skips tokens until reaching a safe recovery point. Synchronization points SHALL include statement boundaries (NEWLINE, DEDENT), declaration keywords, and end-of-file.

#### Scenario: Synchronization to NEWLINE boundary
- **WHEN** an error occurs mid-statement
- **THEN** the parser SHALL skip tokens until finding NEWLINE, DEDENT, or EOF

#### Scenario: Synchronization to declaration keyword
- **WHEN** an error occurs in a declaration
- **THEN** the parser SHALL skip until finding a declaration keyword (TRAIT, SYSTEM, FUNC, STRUCT, ENUM, MODULE, USE, CONST, EVENT, UNIT, TEMPLATE, VIEW, INTERFACE, ASSET, INPUT)

#### Scenario: Synchronization to DEDENT boundary
- **WHEN** an error occurs inside an indented block
- **THEN** the parser SHALL skip tokens until finding DEDENT or EOF to exit the block

#### Scenario: Synchronization stops at EOF
- **WHEN** synchronization is seeking a recovery point
- **THEN** the parser SHALL stop at EOF_TOKEN and not read past the end of the token stream

### Requirement: Parser SHALL support multi-error reporting
The parser SHOULD continue parsing after encountering errors to report multiple syntax errors in a single compilation pass, when possible without compromising error recovery quality.

#### Scenario: Multiple independent errors reported in one pass
- **WHEN** a source file contains syntax errors in multiple independent declarations
- **THEN** the parser SHALL report errors for each declaration rather than stopping at the first error

#### Scenario: Parser continues after struct parsing error
- **WHEN** a struct definition has a syntax error
- **THEN** the parser SHALL synchronize, report the error, and attempt to parse subsequent declarations

#### Scenario: Cascading errors are minimized
- **WHEN** an error occurs and synchronization happens
- **THEN** the parser SHALL NOT report spurious errors for tokens that were skipped during synchronization

### Requirement: Parser error messages SHALL indicate error location and expected tokens
When the parser encounters an unexpected token, error messages MUST include the source location and indicate what tokens were expected. This requirement applies to both the existing `consume()` method errors and any new synchronization error messages.

#### Scenario: Error message includes source location
- **WHEN** `consume()` fails with an unexpected token
- **THEN** the error message SHALL include the file, line, and column of the unexpected token

#### Scenario: Error message indicates expected token
- **WHEN** `consume(COLON, "expected ':'")` fails
- **THEN** the error message SHALL indicate that `:` was expected and what was found instead

#### Scenario: Synchronization does not report additional errors for skipped tokens
- **WHEN** the parser synchronizes by skipping multiple tokens
- **THEN** the parser SHALL NOT report separate errors for each skipped token, only for the original unexpected token
