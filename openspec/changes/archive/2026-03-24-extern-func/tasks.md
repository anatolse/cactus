## 1. Lexer — EXTERN keyword

- [ ] 1.1 Add `EXTERN` to the `TokenType` enum in `src/frontend/token.h`
- [ ] 1.2 Add `"extern"` → `TokenType::EXTERN` to the keyword map in `src/frontend/lexer.cpp`
- [ ] 1.3 Add lexer test: `extern` tokenizes as EXTERN; `extern_value` tokenizes as IDENTIFIER

## 2. AST — FuncNode.is_extern flag

- [ ] 2.1 Add `bool is_extern = false` field to `FuncNode` in `src/frontend/ast.h`

## 3. Parser — extern func declaration path

- [ ] 3.1 In `parse_declaration()`, add handling for `EXTERN` token: when `pub` has been consumed and `EXTERN` follows, or when `EXTERN` appears at the top-level, dispatch to a new `parse_extern_func(is_pub)` helper
- [ ] 3.2 Implement `parse_extern_func(bool is_pub)` in `src/frontend/parser.cpp`: consume `EXTERN`, consume `FUNC`, parse name, param list, optional type_ref (no arrow), call `expect_newline()` — no colon, no block; return `FuncNode` with `is_extern = true` and empty body
- [ ] 3.3 Add parser tests: `pub extern func lerp(a, b, t: float) float` parses to FuncNode with `is_extern = true`; consecutive extern funcs parse cleanly; non-extern func without body still errors

## 4. Semantic Analyzer — ResolvedFunc and extern exemptions

- [ ] 4.1 Add `ResolvedParam` and `ResolvedFunc` structs to `src/frontend/semantic_analyzer.h`
- [ ] 4.2 Add `std::unordered_map<std::string, ResolvedFunc> funcs` to `DecoratedProgram` in `src/frontend/semantic_analyzer.h`
- [ ] 4.3 Add `std::unordered_map<std::string, ResolvedFunc> funcs` to `ImportedSymbols` in `src/frontend/semantic_analyzer.h`
- [ ] 4.4 In `resolve_all_types()` (Phase 2), iterate `FuncNode` declarations: resolve param types and return type, construct `ResolvedFunc`, insert into `result_.funcs`
- [ ] 4.5 In `check_func_purity()`, skip funcs where `fn->is_extern == true`
- [ ] 4.6 In `check_no_recursion()`, skip extern funcs when building the call graph (do not add call-graph entries for body-less funcs)
- [ ] 4.7 In `validate_stmt_contexts()`, skip extern funcs (no body to walk)
- [ ] 4.8 In `ModuleImports::add()`, extend to also index func providers (by analogy with trait/struct/enum providers) for future qualified func resolution
- [ ] 4.9 Add semantic tests: extern func produces `ResolvedFunc` with `is_extern = true`; extern func not flagged by purity check; user func still enforced

## 5. Module Artifact — funcs section and version bump

- [ ] 5.1 Increment `CURRENT_VERSION` from `1` to `2` in `src/frontend/module_artifact.h`
- [ ] 5.2 Add `write_funcs()` private method to `ModuleArtifact` — serializes `DecoratedProgram.funcs` map (name, is_pub, is_extern, params, return_type)
- [ ] 5.3 Add `read_funcs()` private method to `ModuleArtifact` — deserializes funcs section back to `unordered_map<string, ResolvedFunc>`
- [ ] 5.4 Call `write_funcs()` in `save()` after the enums section
- [ ] 5.5 Call `read_funcs()` in `load()` after the enums section; populate `program.funcs`
- [ ] 5.6 In `extract_pub_symbols()`, read and filter the funcs section — include only `is_pub == true` entries in `ImportedSymbols.funcs`
- [ ] 5.7 Add artifact tests: extern func round-trips through save/load; version-1 artifact rejected with error; `extract_pub_symbols` returns pub extern funcs only

## 6. Backend — cpp-entt runtime include and no extern body

- [ ] 6.1 In `CppEnttCodegen::generate()`, add a helper `has_extern_funcs(program)` that checks `program.funcs` for any entry with `is_extern = true`
- [ ] 6.2 If `has_extern_funcs` returns true, emit `#include "cactus_runtime.h"` after the fixed system includes
- [ ] 6.3 Ensure no code path in `CppEnttCodegen` emits a function body for extern funcs (since backends currently emit nothing for FuncNode, verify this remains so — add a guard if needed)
- [ ] 6.4 Add codegen test: program with extern func generates file containing `#include "cactus_runtime.h"`; program without extern func does not

## 7. Backend — cpp-manual runtime include and no extern body

- [ ] 7.1 In `CppManualCodegen::generate()`, add the same `has_extern_funcs(program)` check
- [ ] 7.2 If extern funcs present, emit `#include "cactus_runtime.h"` after the fixed system includes
- [ ] 7.3 Verify no extern func body is emitted; add guard if needed
- [ ] 7.4 Add codegen test: same as 6.4 for cpp-manual backend

## 8. Parser — remove `->` arrow from func return type syntax

- [ ] 8.1 In `parse_func_return_type()` (or wherever `ARROW` is consumed before the return type in func/extern func parsing), remove the `consume(ARROW)` call — the return type now follows `)` directly
- [ ] 8.2 Add an error if `ARROW` is encountered where a return type or colon is expected in a func declaration: "unexpected '->'; return type follows ')' directly"
- [ ] 8.3 Update existing `.cactus` fixture files that use `-> type` in func declarations (search `tests/fixtures/` and `examples/`)
- [ ] 8.4 Add parser test: `func distance(a: vec3, b: vec3) float:` parses correctly; `func distance(a: vec3, b: vec3) -> float:` produces an error

## 9. Stdlib — update math and input modules

- [ ] 9.1 Update `stdlib/std/math.cactus` — change all `pub func` to `pub extern func`; remove `->` from return types; remove `:` and any dummy bodies
- [ ] 9.2 Update `stdlib/std/math/vec2.cactus` — same
- [ ] 9.3 Update `stdlib/std/math/vec3.cactus` — same
- [ ] 9.4 Update `stdlib/std/math/quat.cactus` — same
- [ ] 9.5 Update `stdlib/std/input.cactus` — change all `pub func` to `pub extern func`; remove `->` from return types; remove colon and dummy return bodies
- [ ] 9.6 Verify all 5 stdlib files parse without error with the updated parser
