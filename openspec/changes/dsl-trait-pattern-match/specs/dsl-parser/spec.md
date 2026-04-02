## ADDED Requirements

### Requirement: Statement-level `match` parsing
The parser SHALL recognize `match expr ":"` at statement position as a `TraitMatchStmt`. This is distinct from the existing `MatchExpr` (expression-level). The trait match arms use `IDENTIFIER ["as" IDENTIFIER] "=>"` syntax; the wildcard arm uses `"_" "=>"`.

```ebnf
trait_match_stmt = "match" expr ":" INDENT trait_match_arm+ DEDENT ;
trait_match_arm  = trait_arm | wildcard_arm ;
trait_arm        = IDENTIFIER ["as" IDENTIFIER] "=>" INDENT stmt+ DEDENT ;
wildcard_arm     = "_" "=>" INDENT stmt+ DEDENT ;
```

The `match` keyword is already in the lexer (used by `MatchExpr`). The parser distinguishes statement vs. expression context by position. At statement position, `match expr:` always produces a `TraitMatchStmt`; type validation (entity_id vs. other) is deferred to semantic analysis.

#### Scenario: Simple trait match with alias parsed
- **WHEN** `match c.other:` followed by `Boss as b =>` and a body is parsed at statement position
- **THEN** the parser produces a `TraitMatchStmt` with one `TraitMatchArm{trait="Boss", alias="b", body=[...]}`

#### Scenario: Trait match with no alias parsed
- **WHEN** `Spike =>` arm appears with no `as` clause
- **THEN** the parser produces `TraitMatchArm{trait="Spike", alias=nullopt, body=[...]}`

#### Scenario: Wildcard arm parsed
- **WHEN** `_ =>` arm appears as last arm
- **THEN** the parser produces a `WildcardArm{body=[...]}`

#### Scenario: Multiple arms parsed in order
- **WHEN** match has `Boss as b =>`, then `EnemyAI as e =>`, then `_ =>`
- **THEN** the `TraitMatchStmt` contains arms in declaration order: `[TraitArm(Boss,b), TraitArm(EnemyAI,e), WildcardArm]`
