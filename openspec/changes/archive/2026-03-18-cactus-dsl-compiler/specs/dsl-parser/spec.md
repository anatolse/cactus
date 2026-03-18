## ADDED Requirements

### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. Supported declarations: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `system`, `view`, `event`, `func`, `interface`.

#### Scenario: Module and trait declarations
- **WHEN** the source contains a `module` declaration followed by a `trait` declaration
- **THEN** the parser produces a ProgramNode containing a ModuleNode and a TraitNode

#### Scenario: Unknown top-level keyword
- **WHEN** the source contains an unrecognized keyword at the top level
- **THEN** the parser reports an error with the source location and expected declaration types

### Requirement: Trait parsing with field modifiers
The parser SHALL parse `trait Name:` blocks containing fields with modifiers (`let`, `var`, `persist`, `sync`, `pub`) and event handlers (`on event_name(params):`). Fields SHALL support default value expressions.

#### Scenario: Trait with persist and sync fields
- **WHEN** the source contains `trait Player:` with fields `persist var health: int = 100` and `sync var position: vec3`
- **THEN** the parser produces a TraitNode with two FieldNodes, the first having persist=true and the second having sync=true

#### Scenario: Trait with event handler
- **WHEN** the source contains `trait Damageable:` with `on damage(amount: int):` block
- **THEN** the parser produces a TraitNode containing an EventHandlerNode with event_name "damage"

### Requirement: Unit parsing with apply, config, and child blocks
The parser SHALL parse `[pub] unit Name:` blocks containing `apply:` (list of traits), optional `config:` (field overrides), and optional `child:` (nested unit references).

#### Scenario: Unit with apply and config
- **WHEN** the source contains `unit Cactus:` with `apply:` listing traits and `config:` with field assignments
- **THEN** the parser produces a UnitNode with an ApplyBlock and a ConfigBlock

### Requirement: System parsing with filter and event handlers
The parser SHALL parse `system Name:` blocks containing `filter:` (trait requirements), optional `target:` (cpu/gpu), and event handlers.

#### Scenario: System with filter and on_tick handler
- **WHEN** the source contains `system Movement:` with `filter: [Position, Velocity]` and `on tick(dt: float):`
- **THEN** the parser produces a SystemNode with a FilterClause containing two trait names and an EventHandlerNode

### Requirement: Expression parsing with precedence
The parser SHALL parse expressions using precedence climbing, supporting binary operators (`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `and`, `or`), unary operators (`not`, `-`), member access (`.`), function calls, and lambda expressions (`param => body`).

#### Scenario: Binary expression with correct precedence
- **WHEN** the source contains `a + b * c`
- **THEN** the parser produces a BinaryExpr with `+` at the root and `*` as the right child

#### Scenario: Lambda expression
- **WHEN** the source contains `x => x * 2`
- **THEN** the parser produces a LambdaExpr with parameter "x" and a BinaryExpr body

### Requirement: Pipeline expression parsing
The parser SHALL parse functional pipeline chains: `collection.map(f).filter(g).reduce(init, h)`.

#### Scenario: Map-filter-reduce chain
- **WHEN** the source contains `items.map(i => i.price).filter(p => p > 0).reduce(0, acc, p => acc + p)`
- **THEN** the parser produces a PipelineExpr with three chained operations

### Requirement: Func parsing with purity contract
The parser SHALL parse `[pub] func name(params) [-> type]:` blocks with a body of statements. The parser SHALL record that these are `func` declarations (purity enforcement is done by the semantic analyzer).

#### Scenario: Pure function with return type
- **WHEN** the source contains `func distance(a: vec3, b: vec3) -> float:`
- **THEN** the parser produces a FuncNode with two parameters and return type float

### Requirement: Const block parsing
The parser SHALL parse `const:` blocks containing name-value assignments where values are string literals, number literals, or hex color literals.

#### Scenario: Const block with strings
- **WHEN** the source contains `const:` with `SHOP_TITLE = "Cactus Shop"` and `MAX_ITEMS = 50`
- **THEN** the parser produces a ConstBlockNode with two ConstAssignment entries

### Requirement: View parsing with UI element tree
The parser SHALL parse `view Name(params):` blocks containing nested UI element declarations.

#### Scenario: View with nested elements
- **WHEN** the source contains `view ShopUI(inventory: list[Item]):` with nested `panel:` and `text:` elements
- **THEN** the parser produces a ViewNode with nested ViewElement children

### Requirement: Match expression parsing
The parser SHALL parse `match expr:` blocks with pattern arms.

#### Scenario: Match on enum
- **WHEN** the source contains `match state:` with arms for different enum variants
- **THEN** the parser produces a MatchExpr with the matched expression and a list of arms
