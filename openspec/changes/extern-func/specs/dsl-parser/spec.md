## MODIFIED Requirements

### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. Supported declarations: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `template`, `system`, `event`, `func`, `extern func`, `asset`, `input`.

Note: `view` and `interface` are not supported top-level declarations in v0.2.

```ebnf
declaration = module_decl | use_decl | const_block | struct_decl
            | enum_decl | trait_decl | unit_decl | template_decl | system_decl
            | event_decl | func_decl | extern_func_decl | asset_decl | input_decl ;
```

#### Scenario: Module and trait declarations
- **WHEN** the source contains a `module` declaration followed by a `trait` declaration
- **THEN** the parser produces a ProgramNode containing a ModuleNode and a TraitNode

#### Scenario: Asset declaration in program
- **WHEN** the source contains `asset PlayerMesh: mesh = "player.glb"` at the top level
- **THEN** the parser produces a ProgramNode containing an `AssetDecl` node

#### Scenario: Input declaration in program
- **WHEN** the source contains an `input Jump: button` declaration at the top level
- **THEN** the parser produces a ProgramNode containing an `InputDecl` node

#### Scenario: Extern func declaration in program
- **WHEN** `pub extern func lerp(a, b, t: float) float` appears at the top level
- **THEN** the parser produces a ProgramNode containing a `FuncNode` with `is_extern = true`

#### Scenario: Unknown top-level keyword
- **WHEN** the source contains an unrecognized keyword at the top level
- **THEN** the parser reports an error with the source location and expected declaration types

### Requirement: Func parsing with purity contract
The parser SHALL parse `[pub] func name(params) [type]:` blocks with a body of statements. The parser SHALL additionally parse `[pub] extern func name(params) [type]` declarations without a colon or body. The `is_extern` flag on `FuncNode` distinguishes the two forms. The `->` arrow token is **not used** in function declarations; the return type follows the closing `)` directly.

```ebnf
func_decl        = [ "pub" ] "func" IDENTIFIER
                   "(" [ param_list ] ")" [ type_ref ]
                   ":" NEWLINE INDENT { statement } DEDENT ;

extern_func_decl = [ "pub" ] "extern" "func" IDENTIFIER
                   "(" [ param_list ] ")" [ type_ref ] NEWLINE ;
```

#### Scenario: Pure function with return type (no arrow)
- **WHEN** the source contains `func distance(a: vec3, b: vec3) float:`
- **THEN** the parser produces a FuncNode with `is_extern = false`, two parameters, and return type float

#### Scenario: Extern func with return type, no body, no arrow
- **WHEN** the source contains `pub extern func lerp(a, b, t: float) float`
- **THEN** the parser produces a FuncNode with `is_extern = true`, `is_pub = true`, three parameters, return type float, and empty body

#### Scenario: Extern func with no return type
- **WHEN** the source contains `extern func reset()`
- **THEN** the parser produces a FuncNode with `is_extern = true`, no return type, and empty body

#### Scenario: Non-extern func missing body is a parse error
- **WHEN** `func compute(x: float) float` appears without a colon and body
- **THEN** the parser reports an error: "expected ':'"

#### Scenario: Arrow token in func declaration is a parse error
- **WHEN** `func compute(x: float) -> float:` appears (with `->`)
- **THEN** the parser reports an error: "unexpected '->'; return type follows ')' directly without '->' in func declarations"

#### Scenario: Multiple extern funcs on consecutive lines parse correctly
- **WHEN** `pub extern func sin(a: float) float` is immediately followed by `pub extern func cos(a: float) float`
- **THEN** both are parsed as separate FuncNode declarations with no body-collision error
