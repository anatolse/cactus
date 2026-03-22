## ADDED Requirements

### Requirement: Funcs section in `.cmod` binary format
The module artifact binary format SHALL include a `funcs` section that serializes all `ResolvedFunc` entries from `DecoratedProgram.funcs`. The `CURRENT_VERSION` constant SHALL be incremented to 2 to reflect this format change. Artifacts produced with version 1 SHALL be rejected when loaded.

Binary layout addition (after existing enums section):
```
[funcs section]
  uint32: count
  for each func:
    string: name
    bool:   is_pub
    bool:   is_extern
    uint32: param_count
    for each param:
      string: name
      TypeInfo: type
    bool:   has_return_type
    TypeInfo: return_type (if has_return_type)
```

#### Scenario: Extern func round-trips through artifact save/load
- **WHEN** a module containing `pub extern func lerp(a, b, t: float) -> float` is saved to `.cmod` and reloaded
- **THEN** the loaded `DecoratedProgram.funcs["lerp"]` has `is_extern = true`, `is_pub = true`, and correct param/return types

#### Scenario: User func round-trips through artifact
- **WHEN** a module containing `pub func compute(x: float) -> float:` is saved to `.cmod` and reloaded
- **THEN** the loaded `DecoratedProgram.funcs["compute"]` has `is_extern = false`

#### Scenario: Version 1 artifact rejected
- **WHEN** a `.cmod` file with version byte `1` is loaded after this change
- **THEN** the artifact loader reports a version mismatch error and returns `nullopt`

### Requirement: Pub extern funcs in `extract_pub_symbols`
The `extract_pub_symbols` function SHALL read the funcs section and include `pub` extern funcs in the returned `ImportedSymbols.funcs` map.

#### Scenario: extract_pub_symbols includes pub extern funcs
- **WHEN** `extract_pub_symbols` is called on a `.cmod` containing `pub extern func lerp`
- **THEN** `ImportedSymbols.funcs["lerp"]` is present with `is_extern = true` and correct signature
