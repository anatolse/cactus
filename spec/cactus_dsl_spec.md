# Cactus DSL Language Specification

**Version:** 0.1.0
**Status:** Draft

## 1. Overview

Cactus DSL is a declarative, data-oriented language designed for game development. It targets kids (grades 1-5) with simple indentation-based syntax, but is powerful enough for complex 3D simulations. The language follows ECS (Entity Component System) architecture: data is defined in **traits** and **structs**, logic lives in **systems**, and entities are composed as **units**.

## 2. Lexical Structure

### 2.1 Character Set

Source files are UTF-8 encoded. Only ASCII characters are valid in identifiers and keywords. Non-ASCII characters are allowed only inside string literals.

### 2.2 Indentation

Cactus uses significant indentation (spaces only). Tabs are rejected with an error. Each indentation level is 4 spaces. The lexer maintains an indent stack and emits explicit `INDENT` and `DEDENT` tokens.

```
trait Player:        # TRAIT IDENTIFIER COLON NEWLINE INDENT
    var health: int  # VAR IDENTIFIER COLON IDENTIFIER NEWLINE DEDENT
```

### 2.3 Comments

Single-line comments start with `#` (when not followed by hex digits forming a color literal) and extend to end of line.

```
# This is a comment
var x: int  # Inline comment
```

### 2.4 Keywords

```
module  use     const   struct  enum    trait   unit    system
view    event   func    interface
let     var     persist sync    pub
on      emit    if      else    match   return
apply   config  child   filter  target
map     reduce  true    false   as      and     or      not
```

### 2.5 Operators and Punctuation

| Token | Symbol | Token | Symbol |
|-------|--------|-------|--------|
| COLON | `:` | COMMA | `,` |
| DOT | `.` | ARROW | `->` |
| FAT_ARROW | `=>` | ASSIGN | `=` |
| LPAREN | `(` | RPAREN | `)` |
| LBRACKET | `[` | RBRACKET | `]` |
| LBRACE | `{` | RBRACE | `}` |
| PLUS | `+` | MINUS | `-` |
| STAR | `*` | SLASH | `/` |
| PERCENT | `%` | AMPERSAND | `&` |
| PIPE | `\|` | CARET | `^` |
| TILDE | `~` | EQUALS | `==` |
| NOT_EQUALS | `!=` | LESS | `<` |
| GREATER | `>` | LESS_EQ | `<=` |
| GREATER_EQ | `>=` | PLUS_ASSIGN | `+=` |
| MINUS_ASSIGN | `-=` | | |

### 2.6 Literals

#### Integer Literals
Sequence of decimal digits: `0`, `42`, `1000`

#### Float Literals
Decimal digits with a decimal point: `3.14`, `0.5`, `100.0`

#### String Literals
Double-quoted UTF-8 strings: `"Hello World"`. Only valid inside `const` blocks.

#### Hex Color Literals
`#` followed by 6 (RGB) or 8 (RGBA) hex digits: `#FF0000`, `#FF000080`

#### Boolean Literals
`true`, `false`

## 3. Grammar (EBNF)

### 3.1 Program Structure

```ebnf
program         = { declaration } EOF ;
declaration     = module_decl | use_decl | const_block | struct_decl
                | enum_decl | trait_decl | unit_decl | system_decl
                | view_decl | event_decl | func_decl | interface_decl ;
```

### 3.2 Module and Imports

```ebnf
module_decl     = "module" IDENTIFIER NEWLINE ;
use_decl        = "use" IDENTIFIER [ "as" IDENTIFIER ] NEWLINE ;
```

### 3.3 Const Block

```ebnf
const_block     = "const" ":" NEWLINE INDENT
                  { const_assign }
                  DEDENT ;
const_assign    = IDENTIFIER "=" const_value NEWLINE ;
const_value     = STRING_LITERAL | INT_LITERAL | FLOAT_LITERAL | HEX_COLOR ;
```

### 3.4 Struct

```ebnf
struct_decl     = "struct" IDENTIFIER ":" NEWLINE INDENT
                  { field_decl }
                  DEDENT ;
```

### 3.5 Enum

```ebnf
enum_decl       = "enum" IDENTIFIER ":" NEWLINE INDENT
                  { enum_variant }
                  DEDENT ;
enum_variant    = IDENTIFIER [ "=" INT_LITERAL ] NEWLINE ;
```

### 3.6 Trait

```ebnf
trait_decl      = [ "pub" ] "trait" IDENTIFIER ":" NEWLINE INDENT
                  { field_decl | event_handler | func_decl }
                  DEDENT ;
```

### 3.7 Fields

```ebnf
field_decl      = field_modifiers ( "let" | "var" ) IDENTIFIER ":" type_ref
                  [ "=" expression ] NEWLINE ;
field_modifiers = { "persist" | "sync" | "pub" } ;
```

### 3.8 Unit

```ebnf
unit_decl       = [ "pub" ] "unit" IDENTIFIER ":" NEWLINE INDENT
                  apply_block
                  [ config_block ]
                  [ child_block ]
                  DEDENT ;
apply_block     = "apply" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
config_block    = "config" ":" NEWLINE INDENT
                  { config_assign }
                  DEDENT ;
config_assign   = IDENTIFIER "=" expression NEWLINE ;
child_block     = "child" ":" NEWLINE INDENT
                  { child_entry }
                  DEDENT ;
child_entry     = IDENTIFIER IDENTIFIER NEWLINE ;
```

### 3.9 System

```ebnf
system_decl     = "system" IDENTIFIER ":" NEWLINE INDENT
                  filter_clause
                  [ target_clause ]
                  { event_handler }
                  DEDENT ;
filter_clause   = "filter" ":" "[" IDENTIFIER { "," IDENTIFIER } "]" NEWLINE ;
target_clause   = "target" ":" ( "cpu" | "gpu" ) NEWLINE ;
```

### 3.10 Event Handler

```ebnf
event_handler   = "on" IDENTIFIER "(" [ param_list ] ")" ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;
param_list      = param { "," param } ;
param           = IDENTIFIER ":" type_ref ;
```

### 3.11 Event Declaration

```ebnf
event_decl      = "event" IDENTIFIER ":" NEWLINE INDENT
                  { field_decl }
                  DEDENT ;
```

### 3.12 Func

```ebnf
func_decl       = [ "pub" ] "func" IDENTIFIER "(" [ param_list ] ")"
                  [ "->" type_ref ] ":" NEWLINE INDENT
                  { statement }
                  DEDENT ;
```

### 3.13 View

```ebnf
view_decl       = "view" IDENTIFIER "(" [ param_list ] ")" ":" NEWLINE INDENT
                  { view_element }
                  DEDENT ;
view_element    = IDENTIFIER ":" NEWLINE INDENT
                  { view_prop | view_element }
                  DEDENT ;
view_prop       = IDENTIFIER "=" expression NEWLINE ;
```

### 3.14 Interface

```ebnf
interface_decl  = "interface" IDENTIFIER ":" NEWLINE INDENT
                  { func_signature }
                  DEDENT ;
func_signature  = "func" IDENTIFIER "(" [ param_list ] ")"
                  [ "->" type_ref ] NEWLINE ;
```

### 3.15 Types

```ebnf
type_ref        = IDENTIFIER [ "[" type_ref "]" ] ;
```

Built-in type names: `int`, `float`, `bool`, `string`, `vec2`, `vec3`, `quat`, `color`, `entity_id`.
Parameterized: `list[T]` where `T` is any type.

### 3.16 Expressions

```ebnf
expression      = or_expr ;
or_expr         = and_expr { "or" and_expr } ;
and_expr        = equality_expr { "and" equality_expr } ;
equality_expr   = comparison_expr { ( "==" | "!=" ) comparison_expr } ;
comparison_expr = additive_expr { ( "<" | ">" | "<=" | ">=" ) additive_expr } ;
additive_expr   = multiplicative_expr { ( "+" | "-" ) multiplicative_expr } ;
multiplicative_expr = unary_expr { ( "*" | "/" | "%" ) unary_expr } ;
unary_expr      = ( "not" | "-" ) unary_expr | postfix_expr ;
postfix_expr    = primary_expr { "." IDENTIFIER [ "(" [ arg_list ] ")" ] } ;
primary_expr    = INT_LITERAL | FLOAT_LITERAL | STRING_LITERAL | HEX_COLOR
                | BOOL_LITERAL | IDENTIFIER | "(" expression ")"
                | lambda_expr | match_expr | if_expr | list_literal ;
lambda_expr     = IDENTIFIER "=>" expression ;
match_expr      = "match" expression ":" NEWLINE INDENT
                  { match_arm }
                  DEDENT ;
match_arm       = pattern "=>" expression NEWLINE ;
pattern         = IDENTIFIER | INT_LITERAL | "_" ;
if_expr         = "if" expression ":" expression "else" ":" expression ;
list_literal    = "[" [ expression { "," expression } ] "]" ;
arg_list        = expression { "," expression } ;
```

### 3.17 Statements

```ebnf
statement       = var_assign | emit_stmt | return_stmt | expr_stmt | if_stmt ;
var_assign      = IDENTIFIER ( "=" | "+=" | "-=" ) expression NEWLINE ;
emit_stmt       = "emit" IDENTIFIER "(" [ arg_list ] ")" NEWLINE ;
return_stmt     = "return" [ expression ] NEWLINE ;
expr_stmt       = expression NEWLINE ;
if_stmt         = "if" expression ":" NEWLINE INDENT
                  { statement }
                  DEDENT
                  [ "else" ":" NEWLINE INDENT
                    { statement }
                    DEDENT ] ;
```

## 4. Operator Precedence

From lowest to highest:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 (lowest) | `or` | Left |
| 2 | `and` | Left |
| 3 | `==`, `!=` | Left |
| 4 | `<`, `>`, `<=`, `>=` | Left |
| 5 | `+`, `-` | Left |
| 6 | `*`, `/`, `%` | Left |
| 7 (highest) | `not`, `-` (unary) | Right (prefix) |
| 8 | `.` (member access), `()` (call) | Left (postfix) |

## 5. Type System

### 5.1 Primitive Types

| Type | Description | Size |
|------|-------------|------|
| `int` | 32-bit signed integer | 4 bytes |
| `float` | 64-bit floating point | 8 bytes |
| `bool` | Boolean | 1 byte |
| `string` | UTF-8 immutable string (rvalue only) | pool ID |
| `vec2` | `{ x: float, y: float }` | 16 bytes |
| `vec3` | `{ x: float, y: float, z: float }` | 24 bytes |
| `quat` | `{ x: float, y: float, z: float, w: float }` | 32 bytes |
| `color` | RGBA color from hex literal | 4 bytes |
| `entity_id` | Opaque handle to a unit instance | 8 bytes |

### 5.2 Composite Types

- **`struct Name:`** — Value object. Fields only. Passed by value. No identity. No methods.
- **`enum Name:`** — Named set of integer constants. Used for state machines.
- **`list[T]`** — Ordered collection supporting `map`, `filter`, `reduce`. Functional stream.

### 5.3 Field Modifiers

| Modifier | Meaning | Constraint |
|----------|---------|------------|
| `let` | Immutable. Set once at creation. | Cannot be reassigned. |
| `var` | Mutable. Can be changed by systems. | Default mutability. |
| `persist` | Marks field for serialization. | Only on `var` fields. |
| `sync` | Marks field for network replication. | Only on `var` fields. |
| `pub` | Public visibility outside module. | On fields, traits, units, funcs. |

### 5.4 Type Inference

- Lambda parameters are inferred from context (e.g., `items.map(i => i.price)` infers `i: Item`)
- Binary operation results follow standard promotion rules
- Function call return types are resolved from declarations

## 6. Semantic Constraints

### 6.1 Const-String Rule

String literals (`"..."`) are **forbidden** outside `const` blocks. All string references in logic must go through `const` identifiers. This ensures compile-time string interning and prevents accidental allocations in hot loops.

```
# VALID
const:
    SHOP_TITLE = "Cactus Shop"

# INVALID — string literal in system body
system UI:
    on tick(dt: float):
        set_title("Bad")  # ERROR: string literal outside const block
```

### 6.2 Func Purity

Functions declared with `func` are **pure**:
- No `emit` statements allowed
- No mutation of external state
- No `world` access
- Return value only
- No side effects

```
# VALID
func distance(a: vec3, b: vec3) -> float:
    return ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z))

# INVALID
func bad() -> int:
    emit SomeEvent()  # ERROR: emit in func
    return 0
```

### 6.3 No Recursion

Recursive function calls (direct or indirect) are forbidden. This is required for GPU safety and deterministic execution.

### 6.4 No Imperative Loops

There are no `for`, `while`, or `do` loops. All iteration is via functional collection operations:
- `collection.map(f)` — Transform each element
- `collection.filter(pred)` — Keep elements matching predicate
- `collection.reduce(init, accumulator)` — Fold to single value

### 6.5 Persist/Sync Validation

- `persist` modifier is only valid on `var` fields (not `let`)
- `sync` modifier is only valid on `var` fields (not `let`)
- Both `persist` and `sync` can be combined on the same field
- The semantic analyzer validates these constraints and reports errors with source locations

```
# VALID
trait Player:
    persist sync var health: int = 100
    persist var score: int = 0
    let max_health: int = 100  # let is fine without persist/sync

# INVALID
trait Bad:
    persist let name: string  # ERROR: persist on let field
    sync let id: int          # ERROR: sync on let field
```

### 6.6 System Filter Validation

- All trait names in a system's `filter` clause must reference declared traits
- Filter traits must be compatible (no conflicting field names)

### 6.7 Event Validation

- `emit` statements must reference declared events
- Event handler parameter signatures must match event field declarations

## 7. Execution Model

### 7.1 ECS Architecture

- **Traits** define data schemas (components in ECS terminology)
- **Units** compose traits into entity archetypes
- **Systems** contain logic that operates on entities matching trait filters
- **Events** enable decoupled communication between systems

### 7.2 System Execution

Systems execute their event handlers each frame. The `on tick(dt: float)` handler runs every frame. Other handlers run when their corresponding events are emitted.

### 7.3 Presentation

Raylib is the default rendering/presentation API. Generated code uses Raylib for window management, rendering, input, and audio. Alternative libraries (SDL, etc.) can be supported via backend configuration.
