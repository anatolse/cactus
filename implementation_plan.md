# Implementation Plan

[Overview]
Build a Cactus DSL compiler: a C++ frontend that parses `.cactus` files into a decorated AST, plus a C++ backend that generates high-performance ECS game code in two flavors (manual SoA and EnTT-based).

Cactus DSL is a declarative, data-oriented language designed for game development. It targets kids (grades 1-5) with simple YAML-like indentation syntax, but is powerful enough for complex 3D simulations. The language eliminates imperative loops in favor of functional collection operations (`map`, `filter`, `reduce`), enforces string-literal safety through `const` declarations, and separates data (traits/structs) from logic (systems) following ECS architecture. The compiler frontend parses `.cactus` source files, performs semantic analysis (type checking, scope resolution, const-string enforcement), and produces a decorated AST. Two C++ backend code generators then transform this AST into runnable C++ code: one using hand-rolled SoA (Structure of Arrays) data layouts, and another leveraging the EnTT ECS library. The minimal viable target is a 3D third-person cactus shop game. Build system is CMake, tests use Catch2.

[Types]
The Cactus DSL type system is statically typed with type inference at compile time.

### Primitive Types
- `int` — 32-bit signed integer
- `float` — 64-bit floating point (default for all numeric literals with decimals)
- `bool` — `true` / `false`
- `string` — UTF-8 immutable string (rvalue only — can only appear in `const` blocks or as computed expressions, never as inline literals in logic)
- `vec2` — `{ x: float, y: float }`
- `vec3` — `{ x: float, y: float, z: float }`
- `quat` — `{ x: float, y: float, z: float, w: float }`
- `color` — `#RRGGBB` or `#RRGGBBAA` hex literal
- `entity_id` — opaque handle to a unit instance

### Composite Types
- `struct Name:` — Value object. Fields only. Passed by value. No identity. No methods.
- `enum Name:` — Named set of integer constants. Used for state machines.
- `list[T]` — Ordered collection supporting `map`, `filter`, `reduce`. Not a raw array — a functional stream.

### Type Modifiers (on fields inside `trait` or `struct`)
- `let` — Immutable. Set once at creation. Cannot be changed at runtime.
- `var` — Mutable. Can be changed by systems and event handlers.
- `persist` — Modifier on `var`. Marks field for automatic serialization (save/load).
- `sync` — Modifier on `var`. Marks field for automatic network replication.
- `pub` — Visibility modifier. Makes element accessible outside its module.

### Special Constraints
- String literals (`"..."`) are **forbidden** inside `trait`, `unit`, `system`, `func`, and `view` bodies. They may only appear in `const` blocks. All string references in logic must go through `const` identifiers.
- `func` functions are **pure**: no `emit`, no mutation of external state, no `world` access. Return value only.
- `emit` is forbidden inside `func`. Allowed only in `system` and `view` event handlers.
- No recursion in `func` (required for GPU safety).
- No `for`/`while`/`do` loops. All iteration via `map`/`filter`/`reduce`.

### AST Type Representation (C++ frontend)
```cpp
enum class TypeKind {
    Int, Float, Bool, String, Vec2, Vec3, Quat, Color, EntityId,
    Struct,    // user-defined struct
    Enum,      // user-defined enum
    List,      // list[T] — parameterized
    Func,      // function signature
    Void,      // no return value
    Unknown    // pre-resolution placeholder
};

struct TypeInfo {
    TypeKind kind;
    std::string name;                    // for Struct/Enum: the user-defined name
    std::shared_ptr<TypeInfo> element;   // for List: the element type
    std::vector<TypeInfo> params;        // for Func: parameter types
    std::shared_ptr<TypeInfo> ret;       // for Func: return type
    bool is_let = false;
    bool is_persist = false;
    bool is_sync = false;
    bool is_pub = false;
};
```

[Files]
Complete file listing for the Cactus DSL compiler project.

### New Files to Create

**Root:**
- `CMakeLists.txt` — Top-level CMake build configuration
- `spec/cactus_dsl_spec.md` — Formal language specification document

**Example Game:**
- `examples/cactus_shop/main.cactus` — Entry point module
- `examples/cactus_shop/world.cactus` — World configuration and terrain
- `examples/cactus_shop/player.cactus` — Player controller and camera
- `examples/cactus_shop/shop.cactus` — Shop items, inventory, purchase logic
- `examples/cactus_shop/ui.cactus` — HUD and shop UI views

**Common (shared utilities):**
- `src/common/source_location.hpp` — Source file/line/column tracking
- `src/common/error_reporter.hpp` — Error/warning reporting interface
- `src/common/error_reporter.cpp` — Error reporter implementation
- `src/common/string_pool.hpp` — Interned string table (compile-time string→ID mapping)
- `src/common/string_pool.cpp` — String pool implementation
- `src/common/types.hpp` — TypeKind enum, TypeInfo struct, built-in type definitions

**Frontend (Lexer → Parser → Semantic Analyzer → Decorated AST):**
- `src/frontend/token.hpp` — Token types enum and Token struct
- `src/frontend/lexer.hpp` — Lexer class declaration
- `src/frontend/lexer.cpp` — Indentation-sensitive tokenizer implementation
- `src/frontend/ast.hpp` — All AST node types (undecorated)
- `src/frontend/parser.hpp` — Parser class declaration
- `src/frontend/parser.cpp` — Recursive descent parser implementation
- `src/frontend/semantic_analyzer.hpp` — Semantic analysis pass declaration
- `src/frontend/semantic_analyzer.cpp` — Type checking, scope resolution, constraint enforcement
- `src/frontend/decorated_ast.hpp` — Decorated AST nodes (with resolved types, scopes, dependency info)

**C++ Backend — Manual SoA:**
- `src/backend_cpp_manual/cpp_manual_codegen.hpp` — Manual SoA code generator declaration
- `src/backend_cpp_manual/cpp_manual_codegen.cpp` — Generates hand-rolled SoA structs, system loops, event buffers
- `src/backend_cpp_manual/soa_emitter.hpp` — SoA struct generation helpers
- `src/backend_cpp_manual/soa_emitter.cpp` — Implementation
- `src/backend_cpp_manual/system_emitter.hpp` — System function generation helpers
- `src/backend_cpp_manual/system_emitter.cpp` — Implementation
- `src/backend_cpp_manual/event_emitter.hpp` — Event buffer generation helpers
- `src/backend_cpp_manual/event_emitter.cpp` — Implementation

**C++ Backend — EnTT:**
- `src/backend_cpp_entt/cpp_entt_codegen.hpp` — EnTT-based code generator declaration
- `src/backend_cpp_entt/cpp_entt_codegen.cpp` — Generates EnTT registry usage, component structs, system views
- `src/backend_cpp_entt/component_emitter.hpp` — EnTT component struct generation
- `src/backend_cpp_entt/component_emitter.cpp` — Implementation
- `src/backend_cpp_entt/system_emitter.hpp` — EnTT system/view generation
- `src/backend_cpp_entt/system_emitter.cpp` — Implementation
- `src/backend_cpp_entt/event_emitter.hpp` — EnTT event dispatcher generation
- `src/backend_cpp_entt/event_emitter.cpp` — Implementation

**CLI Entry Point:**
- `src/main.cpp` — Command-line interface: parse args, run pipeline, select backend

**Tests (Catch2):**
- `tests/CMakeLists.txt` — Test build configuration
- `tests/test_lexer.cpp` — Lexer unit tests (indentation, tokens, keywords)
- `tests/test_parser.cpp` — Parser unit tests (AST structure for each construct)
- `tests/test_semantic.cpp` — Semantic analysis tests (type errors, const enforcement, emit-in-func)
- `tests/test_codegen_manual.cpp` — Manual SoA codegen output verification
- `tests/test_codegen_entt.cpp` — EnTT codegen output verification
- `tests/fixtures/` — Directory for `.cactus` test fixture files
- `tests/fixtures/minimal_trait.cactus`
- `tests/fixtures/simple_system.cactus`
- `tests/fixtures/cactus_shop_mini.cactus`

### Existing Files to Modify
- `openspec/config.yaml` — Add project context (tech stack, conventions)

### Files to Delete
- `gemini_page.html` — Temporary file from investigation, no longer needed

[Functions]
Key functions across the compiler pipeline.

### Lexer (`src/frontend/lexer.hpp/.cpp`)
- `Lexer::Lexer(std::string source, std::string filename)` — Constructor, initializes source and indent stack
- `Lexer::tokenize() -> std::vector<Token>` — Main entry: produces token stream with INDENT/DEDENT
- `Lexer::process_indentation(std::vector<Token>&) -> void` — Handles indent stack push/pop, emits INDENT/DEDENT tokens
- `Lexer::read_identifier() -> Token` — Reads alphanumeric+underscore sequence, matches keywords
- `Lexer::read_number() -> Token` — Reads int or float literal
- `Lexer::read_string() -> Token` — Reads quoted string literal (only valid in const blocks)
- `Lexer::read_hex_color() -> Token` — Reads `#RRGGBB` or `#RRGGBBAA` color literal
- `Lexer::skip_comment() -> void` — Skips from `#` to end of line
- `Lexer::peek_char() -> char` — Look ahead without consuming
- `Lexer::advance_char() -> char` — Consume and return current character

### Parser (`src/frontend/parser.hpp/.cpp`)
- `Parser::Parser(std::vector<Token> tokens)` — Constructor
- `Parser::parse_program() -> ProgramNode` — Top-level: parses sequence of declarations
- `Parser::parse_module() -> ModuleNode` — Parses `module name`
- `Parser::parse_use() -> UseNode` — Parses `use name [as alias]`
- `Parser::parse_const_block() -> ConstBlockNode` — Parses `const:` block with assignments
- `Parser::parse_struct() -> StructNode` — Parses `struct Name:` with fields
- `Parser::parse_enum() -> EnumNode` — Parses `enum Name:` with variants
- `Parser::parse_trait() -> TraitNode` — Parses `trait Name:` with fields and event handlers
- `Parser::parse_unit() -> UnitNode` — Parses `[pub] unit Name:` with apply, config, child blocks
- `Parser::parse_system() -> SystemNode` — Parses `system Name:` with filter, target, event handlers
- `Parser::parse_view() -> ViewNode` — Parses `view Name(params):` with UI element tree
- `Parser::parse_event() -> EventNode` — Parses `event Name:` with fields
- `Parser::parse_func() -> FuncNode` — Parses `func name(params) [-> type]:` with body
- `Parser::parse_interface() -> InterfaceNode` — Parses `interface Name:` with method signatures
- `Parser::parse_field() -> FieldNode` — Parses `[persist] [sync] [pub] (let|var) name: expr`
- `Parser::parse_event_handler() -> EventHandlerNode` — Parses `on event_name(params):` block
- `Parser::parse_expression() -> ExprNode` — Expression parser (precedence climbing)
- `Parser::parse_lambda() -> LambdaNode` — Parses `param => body` inline functions
- `Parser::parse_pipeline() -> PipelineNode` — Parses `collection.map(f).filter(g).reduce(i, h)` chains
- `Parser::parse_match() -> MatchNode` — Parses `match expr:` with arms
- `Parser::parse_if() -> IfNode` — Parses `if cond: ... [else: ...]`
- `Parser::consume(TokenType) -> Token` — Expect and consume token or error
- `Parser::peek() -> Token` — Look ahead
- `Parser::advance() -> Token` — Consume current token

### Semantic Analyzer (`src/frontend/semantic_analyzer.hpp/.cpp`)
- `SemanticAnalyzer::SemanticAnalyzer(ProgramNode& ast)` — Constructor
- `SemanticAnalyzer::analyze() -> DecoratedProgram` — Main entry: runs all passes
- `SemanticAnalyzer::resolve_modules() -> void` — Resolves `use` imports, builds module graph
- `SemanticAnalyzer::resolve_types() -> void` — Resolves all type references (struct names, trait names, list[T])
- `SemanticAnalyzer::check_const_strings() -> void` — Ensures no string literals outside `const` blocks
- `SemanticAnalyzer::check_func_purity() -> void` — Ensures no `emit`, no `world` access, no mutation in `func`
- `SemanticAnalyzer::check_no_recursion() -> void` — Ensures no recursive `func` calls
- `SemanticAnalyzer::resolve_scopes() -> void` — Builds scope tree, resolves variable references
- `SemanticAnalyzer::infer_types() -> void` — Type inference for expressions and lambdas
- `SemanticAnalyzer::build_dependency_graph() -> void` — Builds system dependency graph for parallelism analysis
- `SemanticAnalyzer::validate_system_filters() -> void` — Ensures filter traits exist and are compatible
- `SemanticAnalyzer::validate_event_usage() -> void` — Ensures emitted events are declared, handlers match signatures

### String Pool (`src/common/string_pool.hpp/.cpp`)
- `StringPool::intern(std::string_view str) -> uint64_t` — Returns hash ID for string, stores in pool
- `StringPool::lookup(uint64_t id) -> std::string_view` — Reverse lookup
- `StringPool::contains(std::string_view str) -> bool` — Check if string is already interned

### Error Reporter (`src/common/error_reporter.hpp/.cpp`)
- `ErrorReporter::error(SourceLocation loc, std::string msg) -> void` — Report error with location
- `ErrorReporter::warning(SourceLocation loc, std::string msg) -> void` — Report warning
- `ErrorReporter::has_errors() -> bool` — Check if any errors were reported
- `ErrorReporter::print_summary() -> void` — Print error/warning counts

### C++ Manual Backend (`src/backend_cpp_manual/`)
- `CppManualCodegen::CppManualCodegen(DecoratedProgram& program)` — Constructor
- `CppManualCodegen::generate() -> std::string` — Main entry: produces complete C++ source
- `SoaEmitter::emit_struct(StructNode&) -> std::string` — Generates C++ POD struct
- `SoaEmitter::emit_trait_storage(TraitNode&) -> std::string` — Generates SoA storage class with parallel vectors
- `SoaEmitter::emit_archetype(UnitNode&) -> std::string` — Generates archetype struct combining trait storages
- `SystemEmitter::emit_system(SystemNode&) -> std::string` — Generates system update function with SoA iteration
- `SystemEmitter::emit_map_operation(MapExpr&) -> std::string` — Generates SIMD-friendly loop from `map`
- `SystemEmitter::emit_filter_operation(FilterExpr&) -> std::string` — Generates filtered iteration
- `SystemEmitter::emit_reduce_operation(ReduceExpr&) -> std::string` — Generates accumulation loop
- `EventEmitter::emit_event_struct(EventNode&) -> std::string` — Generates event POD struct
- `EventEmitter::emit_event_buffer(EventNode&) -> std::string` — Generates `std::vector<EventStruct>` buffer
- `EventEmitter::emit_event_dispatch() -> std::string` — Generates event dispatch loop

### C++ EnTT Backend (`src/backend_cpp_entt/`)
- `CppEnttCodegen::CppEnttCodegen(DecoratedProgram& program)` — Constructor
- `CppEnttCodegen::generate() -> std::string` — Main entry: produces complete C++ source using EnTT
- `ComponentEmitter::emit_component(TraitNode&) -> std::string` — Generates EnTT component struct
- `ComponentEmitter::emit_tag(TraitNode&) -> std::string` — Generates empty tag struct for empty traits
- `SystemEmitter::emit_system(SystemNode&) -> std::string` — Generates function using `registry.view<Components...>()`
- `SystemEmitter::emit_map_operation(MapExpr&) -> std::string` — Generates `view.each([](auto& comp) { ... })`
- `SystemEmitter::emit_filter_operation(FilterExpr&) -> std::string` — Generates view with exclude/filter
- `SystemEmitter::emit_reduce_operation(ReduceExpr&) -> std::string` — Generates accumulation over view
- `EventEmitter::emit_event(EventNode&) -> std::string` — Generates EnTT event struct
- `EventEmitter::emit_dispatcher_setup() -> std::string` — Generates `entt::dispatcher` configuration

### CLI (`src/main.cpp`)
- `main(int argc, char** argv) -> int` — Entry point: parse CLI args, run compiler pipeline
- `parse_args(int argc, char** argv) -> CompilerOptions` — Parse command-line options (input file, backend selection, output path)
- `run_pipeline(CompilerOptions& opts) -> int` — Execute: lex → parse → analyze → generate

[Classes]
Key classes in the compiler architecture.

### Frontend Classes

**`Token`** (`src/frontend/token.hpp`)
- Struct with: `TokenType type`, `std::string value`, `SourceLocation location`
- TokenType enum includes: MODULE, USE, CONST, STRUCT, ENUM, TRAIT, UNIT, SYSTEM, VIEW, EVENT, FUNC, INTERFACE, LET, VAR, PERSIST, SYNC, PUB, ON, EMIT, IF, ELSE, MATCH, RETURN, APPLY, CONFIG, CHILD, FILTER, TARGET, MAP, FILTER_OP, REDUCE, IDENTIFIER, INT_LITERAL, FLOAT_LITERAL, STRING_LITERAL, HEX_COLOR, BOOL_LITERAL, COLON, COMMA, DOT, ARROW, FAT_ARROW, LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE, PLUS, MINUS, STAR, SLASH, PERCENT, AMPERSAND, PIPE, CARET, TILDE, EQUALS, NOT_EQUALS, LESS, GREATER, LESS_EQ, GREATER_EQ, AND, OR, NOT, ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN, INDENT, DEDENT, NEWLINE, EOF_TOKEN

**`Lexer`** (`src/frontend/lexer.hpp/.cpp`)
- Members: `std::string source_`, `std::string filename_`, `size_t pos_`, `int line_`, `int col_`, `std::vector<int> indent_stack_`, `ErrorReporter& errors_`
- Produces vector of Token with proper INDENT/DEDENT handling

**`Parser`** (`src/frontend/parser.hpp/.cpp`)
- Members: `std::vector<Token> tokens_`, `size_t current_`, `ErrorReporter& errors_`
- Produces undecorated AST (ProgramNode as root)

**`SemanticAnalyzer`** (`src/frontend/semantic_analyzer.hpp/.cpp`)
- Members: `ProgramNode& ast_`, `ErrorReporter& errors_`, `StringPool& string_pool_`, `std::unordered_map<std::string, ModuleScope> modules_`, `DependencyGraph dep_graph_`
- Produces DecoratedProgram with resolved types, scopes, and dependency information

### AST Node Classes (`src/frontend/ast.hpp`)
All nodes inherit from `ASTNode` base with `SourceLocation location` field.

- `ProgramNode` — Root. Contains: `std::vector<std::unique_ptr<ASTNode>> declarations`
- `ModuleNode` — `std::string name`
- `UseNode` — `std::string module_name`, `std::optional<std::string> alias`
- `ConstBlockNode` — `std::vector<ConstAssignment> assignments` where ConstAssignment = `{name, StringLiteral|NumberLiteral|HexColor}`
- `StructNode` — `std::string name`, `std::vector<FieldNode> fields`
- `EnumNode` — `std::string name`, `std::vector<EnumVariant> variants`
- `TraitNode` — `std::string name`, `bool is_pub`, `std::vector<FieldNode> fields`, `std::vector<EventHandlerNode> handlers`, `std::vector<FuncNode> methods`
- `UnitNode` — `std::string name`, `bool is_pub`, `ApplyBlock apply`, `std::optional<ConfigBlock> config`, `std::vector<ChildBlock> children`
- `SystemNode` — `std::string name`, `FilterClause filter`, `std::optional<std::string> target` (cpu/gpu), `std::vector<EventHandlerNode> handlers`
- `ViewNode` — `std::string name`, `std::vector<ViewParam> params`, `std::vector<ViewElement> elements`
- `EventNode` — `std::string name`, `std::vector<FieldNode> fields`
- `FuncNode` — `std::string name`, `bool is_pub`, `std::vector<FuncParam> params`, `std::optional<TypeRef> return_type`, `std::vector<StmtNode> body`
- `InterfaceNode` — `std::string name`, `std::vector<FuncSignature> methods`
- `FieldNode` — `FieldModifiers modifiers` (let/var/persist/sync/pub), `std::string name`, `TypeRef type`, `std::optional<ExprNode> default_value`
- `EventHandlerNode` — `std::string event_name`, `std::vector<FuncParam> params`, `std::vector<StmtNode> body`
- `ExprNode` — Variant: `BinaryExpr | UnaryExpr | CallExpr | MemberExpr | LambdaExpr | PipelineExpr | MatchExpr | IfExpr | LiteralExpr | IdentExpr | ListExpr`
- `StmtNode` — Variant: `VarAssign | EmitStmt | ReturnStmt | ExprStmt | IfStmt`

### Decorated AST (`src/frontend/decorated_ast.hpp`)
- `DecoratedProgram` — Contains: resolved module graph, all decorated nodes, dependency graph, string pool snapshot
- Each decorated node wraps the original AST node plus: `TypeInfo resolved_type`, `ScopeId scope`, `std::optional<uint64_t> const_string_id` (for string pool references)

### Backend Classes

**`CppManualCodegen`** (`src/backend_cpp_manual/cpp_manual_codegen.hpp/.cpp`)
- Members: `DecoratedProgram& program_`, `std::stringstream output_`
- Generates: POD structs, SoA storage classes, system functions, event buffers, main game loop

**`CppEnttCodegen`** (`src/backend_cpp_entt/cpp_entt_codegen.hpp/.cpp`)
- Members: `DecoratedProgram& program_`, `std::stringstream output_`
- Generates: EnTT component structs, registry setup, system functions using `registry.view<>()`, dispatcher setup

### Utility Classes

**`StringPool`** (`src/common/string_pool.hpp/.cpp`)
- Members: `std::unordered_map<std::string, uint64_t> str_to_id_`, `std::unordered_map<uint64_t, std::string> id_to_str_`, `uint64_t next_id_`

**`ErrorReporter`** (`src/common/error_reporter.hpp/.cpp`)
- Members: `std::vector<Diagnostic> diagnostics_`, `int error_count_`, `int warning_count_`

**`SourceLocation`** (`src/common/source_location.hpp`)
- Struct: `std::string filename`, `int line`, `int column`

[Dependencies]
External dependencies for the Cactus DSL compiler.

### Build System
- **CMake** >= 3.20 — Build configuration
- **C++20** standard required (for `std::variant`, `std::optional`, `std::string_view`, concepts)

### Required Libraries (fetched via CMake FetchContent)
- **Catch2** v3.x — Testing framework. Used for all unit and integration tests.
  - Source: `https://github.com/catchorg/Catch2.git`
  - Tag: `v3.5.2` (or latest stable)

### Optional Libraries (for EnTT backend)
- **EnTT** v3.x — ECS library. Used only by the EnTT backend code generator and its generated output.
  - Source: `https://github.com/skypjack/entt.git`
  - Tag: `v3.13.1` (or latest stable)
  - Note: EnTT is header-only, so it's only needed to validate generated code compiles correctly.

### No Runtime Dependencies
The compiler itself has no runtime dependencies beyond the C++ standard library. The generated code may depend on EnTT (if EnTT backend is selected) or be fully standalone (if manual backend is selected).

[Testing]
Testing strategy using Catch2 framework.

### Test Structure
All tests live in `tests/` directory. Test fixtures (`.cactus` files) live in `tests/fixtures/`.

### Lexer Tests (`tests/test_lexer.cpp`)
- Tokenization of keywords (all 20+ keywords produce correct TokenType)
- Indentation handling: INDENT/DEDENT tokens generated correctly for nested blocks
- Number literals: int vs float distinction
- String literals: proper tokenization within const blocks
- Hex color literals: `#FF0000` tokenized as HEX_COLOR
- Comment skipping: `# comment` lines produce no tokens
- Error cases: invalid characters, mismatched indentation
- Multi-line: proper NEWLINE token generation

### Parser Tests (`tests/test_parser.cpp`)
- Each top-level construct produces correct AST node type
- `module` declaration parsing
- `const` block with multiple assignments
- `struct` with typed fields
- `trait` with let/var/persist/sync fields and event handlers
- `unit` with apply list and config block
- `system` with filter clause and event handlers
- `func` with parameters, return type, and body
- `event` with fields
- `view` with nested UI elements
- Expression parsing: binary ops, member access, function calls, lambdas
- Pipeline parsing: `collection.map(f).filter(g).reduce(i, h)`
- `match` expression with arms
- Error recovery: meaningful error messages for common mistakes

### Semantic Analysis Tests (`tests/test_semantic.cpp`)
- Type resolution: struct and trait names resolve correctly
- Const string enforcement: string literal in trait body → error
- Func purity: `emit` inside `func` → error
- Func purity: `world` access inside `func` → error
- No recursion: recursive func call → error
- Scope resolution: variables resolve to correct declarations
- Type inference: lambda parameter types inferred from context
- System filter validation: non-existent trait in filter → error
- Event validation: emit of undeclared event → error
- Module resolution: `use` of non-existent module → error

### Code Generation Tests (`tests/test_codegen_manual.cpp`, `tests/test_codegen_entt.cpp`)
- Simple trait → correct SoA storage struct (manual) or component struct (EnTT)
- Simple system with map → correct iteration loop
- Event declaration → correct buffer struct
- Const block → correct string pool / constexpr definitions
- Unit with apply → correct archetype (manual) or entity creation (EnTT)
- Full mini-program → compilable C++ output (validated by attempting compilation)

### Test Fixtures (`tests/fixtures/`)
- `minimal_trait.cactus` — Single trait with one var field
- `simple_system.cactus` — System with filter and map
- `cactus_shop_mini.cactus` — Minimal version of the cactus shop game

[Implementation Order]
Logical sequence of implementation steps to minimize conflicts and ensure incremental progress.

1. **Project scaffolding**: Create CMakeLists.txt, directory structure, fetch Catch2 via FetchContent. Verify empty project builds.

2. **Language specification document**: Write `spec/cactus_dsl_spec.md` with formal EBNF grammar, keyword table, type system rules, execution model, and semantic constraints. This is the reference for all subsequent implementation.

3. **Example game files**: Write the cactus shop example in `.cactus` files. These serve as the "acceptance test" — the compiler must eventually parse and generate code for these.

4. **Common utilities**: Implement `SourceLocation`, `ErrorReporter`, `StringPool`, `TypeInfo`. These are used by all subsequent components.

5. **Lexer**: Implement indentation-sensitive tokenizer. Write Catch2 tests. This is the foundation — everything depends on correct tokenization.

6. **AST node types**: Define all AST node structs/classes in `ast.hpp`. No implementation needed — just data structures.

7. **Parser**: Implement recursive descent parser producing AST from token stream. Write Catch2 tests for each construct. Start with `module`, `const`, `struct`, `trait`, then `unit`, `system`, `func`, `event`, `view`.

8. **Semantic analyzer**: Implement type resolution, scope resolution, constraint checking. Write Catch2 tests. This produces the DecoratedProgram.

9. **C++ Manual Backend**: Implement SoA code generator. Start with struct/trait generation, then systems, then events. Write tests verifying generated C++ is syntactically correct.

10. **C++ EnTT Backend**: Implement EnTT-based code generator. Reuse patterns from manual backend but target EnTT API. Write tests.

11. **CLI entry point**: Wire everything together in `main.cpp` with argument parsing (input file, `--backend manual|entt`, `--output path`).

12. **Integration test**: Compile the cactus shop example through the full pipeline and verify the generated C++ code compiles with a C++20 compiler.
