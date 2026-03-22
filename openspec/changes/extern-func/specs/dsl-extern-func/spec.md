## ADDED Requirements

### Requirement: `extern func` declaration syntax
The DSL SHALL support `extern func` as a top-level declaration form. An `extern func` declaration introduces a named function whose implementation is provided by the backend runtime, not written in Cactus. The declaration consists of the optional `pub` modifier, the `extern` keyword, the `func` keyword, the function name, a parameter list, and an optional return type. It has **no colon and no body**. The `->` arrow is **not used**; the return type follows the closing `)` directly.

```ebnf
extern_func_decl = [ "pub" ] "extern" "func" IDENTIFIER
                   "(" [ param_list ] ")" [ type_ref ] NEWLINE ;
```

#### Scenario: Pub extern func declaration parsed
- **WHEN** `pub extern func lerp(a, b, t: float) float` appears at the top level
- **THEN** the parser produces a `FuncNode` with `is_extern = true`, `is_pub = true`, `name = "lerp"`, three params, `return_type = float`, and an empty body

#### Scenario: Non-pub extern func declaration parsed
- **WHEN** `extern func lerp(a, b, t: float) float` appears at the top level
- **THEN** the parser produces a `FuncNode` with `is_extern = true`, `is_pub = false`, and an empty body

#### Scenario: Extern func with no return type parsed
- **WHEN** `pub extern func init()` appears at the top level
- **THEN** the parser produces a `FuncNode` with `is_extern = true` and `return_type = nullopt`

#### Scenario: Extern func colon rejected
- **WHEN** `pub extern func lerp(a, b, t: float) float:` appears (with colon)
- **THEN** the parser treats the colon as the start of the next declaration; no body is parsed for the extern func

### Requirement: Extern func is body-less
An `extern func` declaration SHALL NOT have a body. The compiler SHALL NOT require or parse a colon or indented block following an `extern func` signature.

#### Scenario: Extern func with subsequent declaration parses cleanly
- **WHEN** `pub extern func sin(a: float) float` is followed immediately by `pub extern func cos(a: float) float` on the next line
- **THEN** both are parsed as separate `FuncNode` declarations with no error

#### Scenario: Regular func body still required
- **WHEN** `pub func compute(x: float) float` appears without a colon and body
- **THEN** the parser reports an error: "expected ':'" (non-extern funcs require a body)

### Requirement: Extern func is exempt from purity and recursion checks
The semantic analyzer SHALL skip purity enforcement and recursion detection for `extern func` declarations. These checks apply only to user-defined funcs with Cactus bodies.

#### Scenario: Extern func not reported as impure
- **WHEN** `pub extern func play_sound(id: sound_id)` is declared (which might have C++ side effects)
- **THEN** the semantic analyzer does NOT report a purity violation

#### Scenario: Extern func not reported as recursive
- **WHEN** an extern func `lerp` is called from a user-defined func that is also named `lerp` in call-graph terms
- **THEN** the recursion check does NOT flag this as a cycle (extern funcs have no call graph entries)

### Requirement: Extern func exported in `ImportedSymbols`
When a module declares `pub extern func` declarations, those functions SHALL be included in the module's `ImportedSymbols.funcs` map when the module is compiled to a `.cmod` artifact and subsequently imported by another module.

#### Scenario: Pub extern func visible to importing module
- **WHEN** module `std.math` declares `pub extern func lerp(a, b, t: float) float`
- **THEN** after `use std.math as m`, `m.lerp` is a recognized callable symbol in the importing module

#### Scenario: Non-pub extern func not exported
- **WHEN** a module declares `extern func internal_helper()` without `pub`
- **THEN** the function SHALL NOT appear in the module's `ImportedSymbols.funcs` map

### Requirement: Extern func triggers runtime header include in backend output
When the `DecoratedProgram` contains any extern func (in the module itself or in any imported module), the C++ backends SHALL emit `#include "cactus_runtime.h"` in the generated file.

#### Scenario: Runtime header emitted when extern func present
- **WHEN** a module imports `std.math` (which contains extern funcs) and the program is compiled to C++
- **THEN** the generated C++ file contains `#include "cactus_runtime.h"` near the top

#### Scenario: Runtime header not emitted when no extern func present
- **WHEN** a module contains only traits, units, and systems — no extern funcs in scope
- **THEN** the generated C++ file does NOT contain `#include "cactus_runtime.h"`

### Requirement: `EXTERN` is a reserved keyword
The lexer SHALL recognize `extern` as a reserved keyword (`EXTERN` token). It SHALL NOT be usable as an identifier.

#### Scenario: `extern` tokenized as keyword
- **WHEN** the source contains the text `extern`
- **THEN** the lexer produces a token with type `EXTERN` (not `IDENTIFIER`)

#### Scenario: `extern_value` still an identifier
- **WHEN** the source contains the text `extern_value`
- **THEN** the lexer produces a token with type `IDENTIFIER`
