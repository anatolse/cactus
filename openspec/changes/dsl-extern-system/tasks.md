## 1. AST Changes

- [ ] 1.1 Add `ExternSystemNode { name: string, filter: FilterClause, exclude: FilterClause, order_by: vector<SortKey>, after_systems: vector<string>, location: SourceLocation }` to `src/frontend/ast.h`
- [ ] 1.2 Add `ExternSystemNode` to the `Declaration` variant in `ProgramNode`

## 2. Lexer Changes

- [ ] 2.1 No new keywords needed — `extern` is already a keyword. Verify that `extern system` sequence is correctly tokenized as `TOKEN_EXTERN TOKEN_SYSTEM`.

## 3. Parser Changes

- [ ] 3.1 Add `parseExternSystemDecl()` to `src/frontend/parser.cpp`: parse `extern system IDENTIFIER :` INDENT then optional filter, exclude, order_by, after clauses DEDENT (no handlers allowed)
- [ ] 3.2 Hook into the top-level declaration dispatch: when `extern` is followed by `system`, call `parseExternSystemDecl()`
- [ ] 3.3 Report a parse/semantic error if a handler appears inside an `extern system` body
- [ ] 3.4 Update `src/frontend/parser.h` with the new declaration
- [ ] 3.5 Update `tests/test_parser.cpp`: add tests for extern system with filter, order by, after, and error case (handlers inside extern system)

## 4. Semantic Analyzer Changes

- [ ] 4.1 Add `ExternSystemNode` case to `visitDeclaration()` in `src/frontend/semantic_analyzer.cpp`
- [ ] 4.2 Validate filter clause is present (extern system without filter is an error)
- [ ] 4.3 Validate filter aliases against declared traits (same as regular system validation)
- [ ] 4.4 Validate `order by:` keys (same as regular system validation — requires dsl-system-order-by to be implemented first)
- [ ] 4.5 Validate `after:` references name existing systems (regular or extern)
- [ ] 4.6 Validate no ordering cycles including extern systems
- [ ] 4.7 Update `src/frontend/semantic_analyzer.h`
- [ ] 4.8 Update `tests/test_semantic.cpp`: add extern system validation tests

## 5. EnTT Backend Codegen

- [ ] 5.1 Add `ExternSystemNode` handling to `src/backends/cpp-entt/system_emitter.cpp`
- [ ] 5.2 Implement the stdlib known-pattern table: check if the filter's qualified trait names match known stdlib patterns; if yes, generate the optimized implementation
- [ ] 5.3 For stdlib `Renderer` + `Position` pattern: generate a batched sprite rendering loop (sort by layer, iterate, call backend's `render_2d_sprite` implementation)
- [ ] 5.4 For stdlib `AnimatedSprite` pattern: generate frame advancement logic
- [ ] 5.5 For user-defined extern systems (non-stdlib filter): generate the C++ scaffold header declaration + typed iteration loop that calls `<SystemName>_update()`
- [ ] 5.6 Ensure `order by:` in extern systems generates the same sort call as regular systems
- [ ] 5.7 Auto-include stdlib extern systems in the generated output when their traits are present in the program
- [ ] 5.8 Update `tests/test_codegen_entt.cpp`: add tests for stdlib SpriteRenderer codegen and user extern system scaffold codegen

## 6. Stdlib Updates

- [ ] 6.1 Add `extern system SpriteRenderer:` to `stdlib/std/render/sprites.cactus` with filter for `Position` + `Renderer`, `order by: r.layer asc, pos.pos.y asc`
- [ ] 6.2 Add `extern system AnimatedSpriteSystem:` to `stdlib/std/render/sprites.cactus` with filter for `AnimatedSprite`
- [ ] 6.3 Add `extern system MeshRenderer:` to `stdlib/std/render/meshes.cactus` with filter for `Transform` + `Renderer`
- [ ] 6.4 Add `extern system PointLightSystem:` and `extern system DirectionalLightSystem:` to `stdlib/std/render/meshes.cactus`
- [ ] 6.5 Review `stdlib/std/physics/flat.cactus` and `stdlib/std/physics/volume.cactus` for extern system opportunities
- [ ] 6.6 Review `stdlib/std/audio.cactus` for extern system opportunities

## 7. Documentation

- [ ] 7.1 Update `examples/dsl_showcase.cactus` to demonstrate `extern system` — add a `MyParticleSystem` example showing user-defined extern system declaration
- [ ] 7.2 Add a comment to `stdlib/std/render/sprites.cactus` explaining that `SpriteRenderer` runs automatically when `Renderer` is applied to entities
