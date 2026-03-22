## ADDED Requirements

### Requirement: Optional `as` alias in `apply:` blocks of units and templates
A `unit` or `template` `apply:` block entry MAY include an `as IDENTIFIER` alias, identical to the alias syntax supported by system `filter:` clauses. The alias is stored in `ApplyEntry.alias` and used during `config:` key resolution.

```ebnf
apply_entry     = dotted_name [ "as" IDENTIFIER ] [ ":" "disabled" ] NEWLINE ;
```

Example:
```cactus
unit Player:
    apply:
        Position as pos         # alias declared
        Health                  # no alias; trait name is implicit alias
        PlayerController        # no alias
    config:
        pos.position = vec3(0.0, 0.0, 0.0)   # alias-qualified
        Health.health = 100                   # TraitName-qualified
        move_speed = 5.0                      # bare — valid (unique field)
```

#### Scenario: Apply entry with `as` alias is parsed correctly
- **WHEN** `apply:` contains `Position as pos`
- **THEN** `ApplyEntry.alias` is `"pos"` and `ApplyEntry.trait_name` is `"Position"`

#### Scenario: Apply entry without alias has no alias stored
- **WHEN** `apply:` contains `Health` with no `as` clause
- **THEN** `ApplyEntry.alias` is empty/absent and the trait name itself serves as the implicit alias

#### Scenario: `as alias` and `: disabled` may both be present
- **WHEN** `apply:` contains `EnemyAI as ai: disabled`
- **THEN** the parser accepts it with `alias = "ai"` and `initially_active = false`

### Requirement: Qualified `TraitName.field` and `alias.field` keys in `config:` blocks
A `config:` assignment key MAY use a dotted form `IDENTIFIER.IDENTIFIER` where the first component is either an alias declared in the `apply:` block or the bare trait name (implicit alias). The second component is the field name within that trait. Bare (unqualified) keys remain valid.

```ebnf
config_assign   = config_key "=" expression NEWLINE ;
config_key      = IDENTIFIER [ "." IDENTIFIER ] ;
```

The semantic analyzer resolves `config_key` as follows:
1. If the key is a bare `IDENTIFIER`: look up the field across all applied traits. If found in exactly one trait, resolve it. If found in multiple traits, report an ambiguity error.
2. If the key is a dotted `IDENTIFIER.IDENTIFIER`: resolve the first component as a trait alias or trait name from the `apply:` block, then look up the second component as a field of that trait.

```cactus
# Both are valid:
unit Player:
    apply:
        Position as pos
        Health
    config:
        pos.position = vec3(0.0, 0.0, 0.0)  # alias-qualified
        Health.health = 100                  # TraitName-qualified
        move_speed = 5.0                     # bare — valid (unique)
```

#### Scenario: Bare config key resolved when unambiguous
- **WHEN** `config:` contains `health = 100` and only one applied trait has a field `health`
- **THEN** the semantic analyzer resolves the key to that field without error

#### Scenario: Ambiguous bare config key rejected
- **WHEN** two applied traits both declare a field named `pos` and `config:` contains bare `pos = ...`
- **THEN** the semantic analyzer reports: "ambiguous field 'pos' in config; qualify as 'TraitA.pos' or 'TraitB.pos'"

#### Scenario: Alias-qualified config key accepted
- **WHEN** `apply:` has `Position as p` and `config:` contains `p.position = vec3(0.0, 0.0, 0.0)`
- **THEN** the key resolves to `Position.position` and the assignment is accepted

#### Scenario: TraitName-qualified config key accepted
- **WHEN** `apply:` has `Health` (no alias) and `config:` contains `Health.health = 100`
- **THEN** the key resolves to `Health.health` and the assignment is accepted

#### Scenario: Unknown first component in dotted key rejected
- **WHEN** `config:` contains `Unknown.field = 5` and `Unknown` is not a declared alias or applied trait name
- **THEN** the semantic analyzer reports: "unknown trait or alias 'Unknown' in config key"

#### Scenario: Unknown field in qualified config key rejected
- **WHEN** `config:` contains `Health.notafield = 5` and `Health` has no field `notafield`
- **THEN** the semantic analyzer reports: "trait 'Health' has no field 'notafield'"

### Requirement: Qualified field keys in `spawn()` override arguments
`spawn` override argument names follow the same resolution rules as `config:` keys: bare names, `TraitName.field`, or `alias.field` are all valid. Bare names must be unambiguous across the template's applied traits.

```cactus
# All three forms valid:
spawn Enemy(patrol_speed = 5.0)                     # bare — valid if unambiguous
spawn Enemy(EnemyAI.patrol_speed = 5.0)             # TraitName-qualified
spawn Enemy(ai.patrol_speed = 5.0)                  # alias-qualified (if alias declared)
```

#### Scenario: Bare spawn override key resolved when unambiguous
- **WHEN** `spawn Enemy(patrol_speed = 5.0)` and only `EnemyAI` has a field `patrol_speed`
- **THEN** the key resolves to `EnemyAI.patrol_speed` without error

#### Scenario: Ambiguous bare spawn override key rejected
- **WHEN** two applied traits both declare a field `speed` and `spawn Foo(speed = 1.0)` uses bare form
- **THEN** the semantic analyzer reports: "ambiguous field 'speed' in spawn override; qualify as 'TraitA.speed' or 'TraitB.speed'"

#### Scenario: TraitName-qualified spawn override key accepted
- **WHEN** `spawn Enemy(EnemyAI.patrol_speed = 5.0)` is used
- **THEN** the key resolves correctly to `EnemyAI.patrol_speed`
