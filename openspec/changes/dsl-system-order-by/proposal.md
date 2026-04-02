## Why

Systems iterate over entities in an undefined order by default. For rendering, this means sprites on different layers may draw in the wrong order, and Y-sorting (depth illusion) is impossible without manual hacks. For other systems, processing priority cannot be expressed in the DSL. An `order by:` clause on systems provides a declarative, per-system entity sort key, enabling render layers, Y-sort, and priority-ordered processing.

## What Changes

- Add an optional `order by:` block to `system` declarations
- The `order by:` block lists one or more sort keys as `alias.field [asc|desc]` expressions
- Sort keys must reference traits listed in the system's `filter:` block
- Sort key types are restricted to scalar-comparable types: `int`, `float`, `bool`
- Default sort direction is `asc` when omitted
- Multiple sort keys produce lexicographic ordering (first key primary, subsequent keys break ties)
- Sorting runs once per handler invocation, before iteration, each frame

## Capabilities

### New Capabilities
- `dsl-system-order-by`: The `order by:` clause on system declarations for per-system entity sort ordering, including multi-key lexicographic sort, `asc`/`desc` directions, and compile-time validation of sort key types and scope

### Modified Capabilities
- `dsl-parser`: New `order_by_clause` production in the `system_decl` grammar
- `dsl-semantic-analysis`: Validation rules for sort key expressions (must be filter-alias field access, must be scalar-comparable type)
- `backend-cpp-entt`: Code generation for `order by:` — emit `registry.sort<T>(comparator)` before view iteration

## Impact

- `src/frontend/ast.h`: Add `SortKey { alias: string, field: string, descending: bool }` struct; add `order_by: vector<SortKey>` to `SystemNode`
- `src/frontend/parser.cpp/h`: New `parseOrderByClause()` method
- `src/frontend/semantic_analyzer.cpp/h`: New `validateOrderByClause()` validation
- `src/backends/cpp-entt/system_emitter.cpp`: Generate sort call before view iteration when `order_by` is non-empty
- Test files: `test_parser.cpp`, `test_semantic.cpp`, `test_codegen_entt.cpp`
