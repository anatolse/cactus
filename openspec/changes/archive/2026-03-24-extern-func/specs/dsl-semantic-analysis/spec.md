## MODIFIED Requirements

### Requirement: Func purity enforcement
The semantic analyzer SHALL verify that user-defined `func` declarations (those with `is_extern = false`) are pure: no `emit` statements, no mutation of external state, no `world` access. Violations SHALL produce a compile error. `extern func` declarations (those with `is_extern = true`) SHALL be skipped entirely — purity is not enforced over backend-provided functions.

#### Scenario: Emit in func rejected
- **WHEN** a non-extern `func` body contains an `emit` statement
- **THEN** the analyzer reports an error "emit is not allowed in pure functions"

#### Scenario: Extern func skipped by purity check
- **WHEN** `pub extern func play_sfx(id: sound_id)` is declared (which may have side effects in C++)
- **THEN** the analyzer does NOT report a purity violation for it

### Requirement: No recursion in func
The semantic analyzer SHALL detect recursive calls in non-extern `func` declarations (direct and indirect) and report them as errors. `extern func` declarations SHALL be excluded from the call graph — calling an extern func from a user func does not create a call-graph edge for the extern func itself.

#### Scenario: Direct recursion rejected
- **WHEN** `func factorial(n: int) -> int:` calls `factorial(n - 1)` in its body
- **THEN** the analyzer reports an error "recursion is not allowed in func declarations"

#### Scenario: Extern func call does not cause false recursion error
- **WHEN** a user func calls an extern func of the same name (e.g. wrapping it)
- **THEN** the analyzer does NOT report a recursion error

## ADDED Requirements

### Requirement: `ResolvedFunc` produced in `DecoratedProgram`
The semantic analyzer SHALL populate a `funcs` map in `DecoratedProgram` containing a `ResolvedFunc` entry for every `func` and `extern func` declaration in the analyzed program. The `ResolvedFunc` struct SHALL include: `name`, `is_pub`, `is_extern`, resolved parameter types, and resolved return type.

```
struct ResolvedParam {
    string name;
    TypeInfo type;
};

struct ResolvedFunc {
    string name;
    bool is_pub;
    bool is_extern;
    list<ResolvedParam> params;
    optional<TypeInfo> return_type;
};
```

#### Scenario: Extern func produces ResolvedFunc with is_extern = true
- **WHEN** `pub extern func lerp(a, b, t: float) -> float` is analyzed
- **THEN** `DecoratedProgram.funcs["lerp"]` contains a `ResolvedFunc` with `is_extern = true`, `is_pub = true`, three params of type float, and return type float

#### Scenario: User func produces ResolvedFunc with is_extern = false
- **WHEN** `pub func clamp_int(v, lo, hi: int) -> int:` is analyzed
- **THEN** `DecoratedProgram.funcs["clamp_int"]` contains a `ResolvedFunc` with `is_extern = false`

### Requirement: Pub extern funcs exported in `ImportedSymbols`
The semantic analyzer SHALL include `pub extern func` declarations in the `ImportedSymbols.funcs` map when extracting pub symbols from a module. Non-pub extern funcs SHALL NOT be exported.

#### Scenario: Pub extern func appears in ImportedSymbols
- **WHEN** a module declares `pub extern func lerp(a, b, t: float) -> float` and its pub symbols are extracted
- **THEN** `ImportedSymbols.funcs["lerp"]` is present with `is_extern = true`

#### Scenario: Non-pub extern func not exported
- **WHEN** a module declares `extern func internal()` without `pub`
- **THEN** `ImportedSymbols.funcs` does NOT contain `"internal"`
