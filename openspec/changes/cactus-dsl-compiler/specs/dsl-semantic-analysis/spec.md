## ADDED Requirements

### Requirement: Type resolution
The semantic analyzer SHALL resolve all type references in the AST to concrete TypeInfo objects. Struct names, trait names, enum names, and parameterized types (`list[T]`) SHALL be resolved against declared types. Unresolved type references SHALL produce an error.

#### Scenario: Struct type resolves
- **WHEN** a field references type `Position` and a `struct Position:` is declared
- **THEN** the analyzer resolves the field's type to a TypeInfo with kind=Struct and name="Position"

#### Scenario: Unknown type reference
- **WHEN** a field references type `Foo` and no `struct Foo:` or `trait Foo:` is declared
- **THEN** the analyzer reports an error "unknown type 'Foo'" with the source location

### Requirement: Const-string enforcement
The semantic analyzer SHALL reject string literals (`"..."`) that appear outside of `const` blocks. String literals inside `trait`, `unit`, `system`, `func`, `view`, and `event` bodies SHALL produce a compile error.

#### Scenario: String literal in trait body rejected
- **WHEN** a trait field has a default value of `"hello"`
- **THEN** the analyzer reports an error "string literals are only allowed in const blocks"

#### Scenario: String literal in const block accepted
- **WHEN** a `const:` block contains `GREETING = "Hello"`
- **THEN** the analyzer accepts it and interns the string in the StringPool

### Requirement: Func purity enforcement
The semantic analyzer SHALL verify that `func` declarations are pure: no `emit` statements, no mutation of external state, no `world` access. Violations SHALL produce a compile error.

#### Scenario: Emit in func rejected
- **WHEN** a `func` body contains an `emit` statement
- **THEN** the analyzer reports an error "emit is not allowed in pure functions"

#### Scenario: World access in func rejected
- **WHEN** a `func` body references `world` or accesses global mutable state
- **THEN** the analyzer reports an error "world access is not allowed in pure functions"

### Requirement: No recursion in func
The semantic analyzer SHALL detect recursive calls in `func` declarations (direct and indirect) and report them as errors. This is required for GPU safety.

#### Scenario: Direct recursion rejected
- **WHEN** `func factorial(n: int) -> int:` calls `factorial(n - 1)` in its body
- **THEN** the analyzer reports an error "recursion is not allowed in func declarations"

#### Scenario: Indirect recursion rejected
- **WHEN** `func a()` calls `func b()` which calls `func a()`
- **THEN** the analyzer reports an error for the recursive cycle

### Requirement: Scope resolution
The semantic analyzer SHALL build a scope tree and resolve all variable references to their declarations. Undeclared variable references SHALL produce an error. Shadowing within the same scope SHALL produce a warning.

#### Scenario: Variable resolves to declaration
- **WHEN** a system handler references `position` and the filtered trait has a field `position`
- **THEN** the analyzer resolves the reference to that field's declaration

#### Scenario: Undeclared variable
- **WHEN** an expression references `foo` and no `foo` is declared in any enclosing scope
- **THEN** the analyzer reports an error "undeclared identifier 'foo'"

### Requirement: Persist and sync modifier validation
The semantic analyzer SHALL validate that `persist` and `sync` modifiers are only applied to `var` fields (not `let` fields). Using `persist` or `sync` on a `let` field SHALL produce a compile error.

#### Scenario: Persist on var accepted
- **WHEN** a trait field is declared as `persist var health: int`
- **THEN** the analyzer accepts it and sets is_persist=true on the TypeInfo

#### Scenario: Persist on let rejected
- **WHEN** a trait field is declared as `persist let name: string`
- **THEN** the analyzer reports an error "persist modifier can only be used on var fields"

#### Scenario: Sync on var accepted
- **WHEN** a trait field is declared as `sync var position: vec3`
- **THEN** the analyzer accepts it and sets is_sync=true on the TypeInfo

### Requirement: System filter validation
The semantic analyzer SHALL verify that all trait names referenced in system `filter:` clauses correspond to declared traits.

#### Scenario: Valid filter traits
- **WHEN** a system has `filter: [Position, Velocity]` and both traits are declared
- **THEN** the analyzer accepts the filter clause

#### Scenario: Unknown trait in filter
- **WHEN** a system has `filter: [Position, NonExistent]` and `NonExistent` is not declared
- **THEN** the analyzer reports an error "unknown trait 'NonExistent' in system filter"

### Requirement: Event validation
The semantic analyzer SHALL verify that all `emit` statements reference declared events, and that event handler parameter signatures match the event's declared fields.

#### Scenario: Emit of declared event
- **WHEN** a system handler contains `emit Damage(amount: 10)` and `event Damage:` is declared with field `amount: int`
- **THEN** the analyzer accepts the emit statement

#### Scenario: Emit of undeclared event
- **WHEN** a system handler contains `emit Foo()` and no `event Foo:` is declared
- **THEN** the analyzer reports an error "undeclared event 'Foo'"

### Requirement: Dependency graph construction
The semantic analyzer SHALL build a dependency graph of systems based on their trait access patterns (read/write) and event relationships. This graph SHALL be included in the DecoratedProgram for backend optimization.

#### Scenario: Independent systems detected
- **WHEN** system A reads trait Position and system B reads trait Inventory with no overlap
- **THEN** the dependency graph marks them as independent (parallelizable)
