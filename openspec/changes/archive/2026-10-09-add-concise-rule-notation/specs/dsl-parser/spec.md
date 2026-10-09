## MODIFIED Requirements

### Requirement: `filter:` and `exclude:` both optional in rule grammar
Positive and negative unary selection clauses SHALL remain independently optional on rule declarations. The parser SHALL accept positive selection only, negative selection only, both, or neither. `each:` SHALL be the canonical positive spelling and `without:` the canonical negative spelling; `filter:` and `exclude:` SHALL remain accepted equivalents without deprecation warnings. Absence of selection clauses SHALL retain the existing selectionless domain. Bracket-list selection syntax SHALL remain rejected.

Positive entries SHALL support a dotted trait reference and an optional `as` alias. Negative entries SHALL support dotted trait references without aliases. Each list SHALL accept its existing indented form or a nonempty comma-separated inline form, under the shared inline-list requirements.

```ebnf
filter_clause  = ( "filter" | "each" ) ":" trait_list ;
exclude_clause = ( "exclude" | "without" ) ":" name_list ;
trait_entry    = dotted_name [ "as" IDENTIFIER ] ;
trait_list     = NEWLINE INDENT { trait_entry NEWLINE } DEDENT
               | trait_entry { "," trait_entry } [ "," ] NEWLINE ;
name_list      = NEWLINE INDENT { dotted_name NEWLINE } DEDENT
               | dotted_name { "," dotted_name } [ "," ] NEWLINE ;
```

Existing block-form emptiness validation SHALL remain unchanged. `each` and `without` SHALL be contextual words at unary clause positions, not globally reserved words. Declaring both spellings of one canonical clause SHALL be rejected as a duplicate. Pair/unary exclusion and clause-order restrictions SHALL apply identically to both spellings.

#### Scenario: Rule with filter only parsed correctly
- **WHEN** a rule has `filter:` but no `exclude:`
- **THEN** parsing preserves its positive selection and an empty negative selection

#### Scenario: Rule with exclude only parsed correctly
- **WHEN** a rule has `exclude:` but no `filter:`
- **THEN** parsing preserves an empty positive selection and its negative selection

#### Scenario: Rule with no filter or exclude parsed correctly
- **WHEN** a rule has neither positive nor negative selection and no pair domain
- **THEN** parsing preserves the existing selectionless rule

#### Scenario: Old bracket syntax produces parse error
- **WHEN** source contains `filter: [Position, EnemyAI]` or `each: [Position, EnemyAI]`
- **THEN** compilation rejects bracket selection and directs the author to an inline comma-separated list or an indented block

#### Scenario: Filter entry with as alias parsed
- **WHEN** a positive selection contains `Position as pos`
- **THEN** parsing preserves the trait reference and alias `pos`

#### Scenario: Filter entry without alias parsed
- **WHEN** a positive selection contains `Position` without `as`
- **THEN** parsing preserves that reference without an alias

#### Scenario: Filter entry with qualified name and alias
- **WHEN** a positive selection contains `phys.Body as body`
- **THEN** parsing preserves the qualified reference `phys.Body` and alias `body`

#### Scenario: Canonical and legacy inline domains are equivalent
- **WHEN** equivalent rules use `each: Position as pos, Motion` / `without: Frozen` and `filter: Position as pos, Motion` / `exclude: Frozen`
- **THEN** their resolved selections, accesses, and runtime behavior are equivalent apart from source locations

#### Scenario: Contextual words remain identifiers
- **WHEN** valid authored declarations or handler locals use `each` or `without` as an identifier outside a unary clause position
- **THEN** parsing and name resolution retain their existing identifier behavior

#### Scenario: Alias and legacy clause are duplicates
- **WHEN** a rule declares both `each:` and `filter:`, or both `without:` and `exclude:`
- **THEN** compilation reports a duplicate canonical clause without merging its entries

#### Scenario: Canonical unary clauses cannot accompany pairs
- **WHEN** a rule declares `pairs:` with `each:` or `without:`
- **THEN** compilation rejects the combination under the existing single-domain restriction

## ADDED Requirements

### Requirement: Shared inline clause-list surface
Selection, pair-binding trait lists, `after:`, `before:`, `where:`, `when:`, `order by:`, and external `reads:`, `writes:`, `projects:`, `emits:`, `commands:`, and `effects:` SHALL accept nonempty comma-separated inline entries in addition to their block forms. An inline list SHALL end at NEWLINE, SHALL accept one trailing comma and a trailing comment, and SHALL reject empty entries or an indented continuation. Each clause SHALL retain its existing valid declaration positions, entry grammar, semantic checks, and evaluation behavior. An inline clause SHALL NOT make that clause valid in a previously forbidden context. Scalar clauses, payload blocks, and reducer declaration blocks SHALL retain their existing forms.

#### Scenario: Short lists accept comments and one trailing comma
- **WHEN** a rule contains `each: Position as pos, Motion, # selected state`
- **THEN** compilation accepts exactly the two selected traits and the alias

#### Scenario: A pair binding chooses its own list form
- **WHEN** the first pair binding uses an inline trait list and the second uses an indented trait list
- **THEN** compilation accepts both within the same pair declaration

#### Scenario: Empty or repeated inline entries are rejected
- **WHEN** an inline clause starts with a comma or contains two consecutive commas
- **THEN** compilation reports the empty entry at its source location

#### Scenario: Mixed continuation is rejected
- **WHEN** `each: Position` is followed by an indented `Motion` entry before the next clause
- **THEN** compilation rejects the mixed inline/block list

#### Scenario: Extern-only contracts remain extern-only
- **WHEN** a regular handler contains inline `writes: Health` as an authored access annotation
- **THEN** compilation retains the existing rejection of external contract clauses in regular handlers

### Requirement: Inline entries preserve nested expression boundaries
Inline expression lists SHALL separate entries only at the enclosing clause level. Commas and colons inside a valid call, constructor, list literal, or conditional expression SHALL remain part of that expression. Every parsed entry SHALL preserve its own source location and existing scope and type rules.

#### Scenario: Function arguments do not split a predicate list
- **WHEN** `where: touching(body, lava), body != lava` appears with a valid pure `touching` function
- **THEN** compilation parses two predicates, with both arguments belonging to the first predicate

#### Scenario: Nested conditional colon belongs to its expression
- **WHEN** a valid inline predicate uses a conditional expression containing `:`
- **THEN** the conditional expression remains one entry and does not start a payload or continuation block

#### Scenario: Second entry reports its own location
- **WHEN** the second inline predicate has an unknown name or wrong type
- **THEN** its diagnostic points at the second predicate rather than only at the clause header

### Requirement: Malformed compact clauses recover without losing later declarations
Malformed inline lists, pair bindings, and grouped-reduction headers SHALL produce source-located diagnostics and SHALL allow parsing to progress to later clauses, handlers, and top-level declarations. Recovery SHALL NOT reinterpret an invalid continuation as valid entries or loop indefinitely.

#### Scenario: Bad pair binding preserves the next rule
- **WHEN** a malformed compact binding is followed by a valid rule declaration
- **THEN** compilation diagnoses the binding and continues parsing the later rule

#### Scenario: Multiple malformed clauses report multiple errors
- **WHEN** independent inline-list errors appear in two declarations
- **THEN** compilation reports both errors with their respective source locations

### Requirement: Accepted notation preserves canonical program behavior
Changing only an accepted clause spelling or list layout SHALL preserve resolved domains, contracts, dependencies, reduction results, event and command order, structural visibility, and query acceleration decisions. Saving and reloading module artifacts SHALL preserve the same meaning without requiring notation-specific semantic records.

#### Scenario: Compact and block programs observe the same activations
- **WHEN** equivalent compact and legacy programs receive the same frame input and entity changes
- **THEN** their ordered observations, state, and derived-trait lifecycle outcomes are equivalent

#### Scenario: Module artifact round trip preserves compact rules
- **WHEN** compact rules are compiled to module artifacts and linked with dependent modules
- **THEN** their behavior and dependencies equal the corresponding block-form program
