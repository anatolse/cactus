## 1. Frontend syntax and AST

- [x] 1.1 Add an `ExternSystemNode` to the frontend AST and include it in the top-level declaration variant.
- [x] 1.2 Extend the parser to recognize `extern system Name:` declarations and parse `filter:`, `exclude:`, `order by:`, and `after:` clauses without allowing event handlers.
- [x] 1.3 Add parser coverage for valid `extern system` declarations and invalid cases that attempt to include handlers.

## 2. Semantic analysis

- [x] 2.1 Validate `extern system` declarations in semantic analysis, including required `filter:` clauses, name uniqueness, and participation in system ordering checks.
- [x] 2.2 Report the specified diagnostics for unsupported forms such as missing filters or handler-like content in `extern system` declarations.
- [x] 2.3 Add semantic tests for valid extern systems, missing-filter failures, and ordering-cycle behavior involving extern systems.

## 3. EnTT backend code generation

- [x] 3.1 Extend EnTT system emission to include `extern system` declarations in scheduling and code generation.
- [x] 3.2 Implement known stdlib-pattern handling for recognized render/light extern systems and fall back to generated user callback scaffolds for non-stdlib extern systems.
- [x] 3.3 Add backend tests covering known-pattern generation, user-defined scaffold generation, and `order by:` handling in generated extern system code.

## 4. Stdlib integration and verification

- [x] 4.1 Update relevant stdlib render modules to declare passive rendering/light systems as `extern system` definitions.
- [x] 4.2 Add or update integration examples/tests to verify stdlib extern systems are auto-included from module imports.
- [x] 4.3 Run the relevant parser, semantic, and backend test suites and confirm the change passes end-to-end.
