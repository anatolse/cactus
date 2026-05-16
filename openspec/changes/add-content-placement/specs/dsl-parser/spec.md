## ADDED Requirements

### Requirement: Top-level placement declarations

The parser SHALL accept top-level `place` declarations that instantiate a named placement from a template reference with nested trait override entries.

```ebnf
declaration = ... | place_decl ;

place_decl = "place" IDENTIFIER "from" dotted_name ":" NEWLINE INDENT
             { archetype_trait_entry }
             DEDENT ;
```

#### Scenario: Placement from local template parsed
- **WHEN** source contains `place Gem1 from BlueGem:` followed by nested trait override entries
- **THEN** the parser produces a `PlaceDecl` with name `Gem1`, template reference `BlueGem`, and the parsed override entries

#### Scenario: Placement from qualified template parsed
- **WHEN** source contains `place Gem1 from items.BlueGem:`
- **THEN** the parser records the template reference as the dotted name `items.BlueGem`

#### Scenario: Place is top-level only
- **WHEN** `place Gem1 from BlueGem:` appears inside a system handler
- **THEN** the parser reports that placement declarations are only valid at the top level
