## Why

The current DSL has `extern func` for functions whose bodies are provided by the backend. There is no equivalent for *systems*. As a result, rendering and other backend-managed operations must either be hand-authored by game developers (explicit `draw_rect` calls — wrong level of abstraction) or hardcoded by trait name inside the backend (brittle, not extensible).

The `extern system` declaration resolves this gap: it declares a system whose **implementation is provided by the backend**, not authored in DSL. The author provides the filter (what to iterate), optional sort order, and optional ordering constraints. The backend generates the full implementation — iteration boilerplate, batched rendering, physics, audio, etc.

This enables two things:
1. **Stdlib passive systems**: the stdlib declares `extern system SpriteRenderer` once. Developers simply apply `Renderer` to their entities and rendering happens automatically, without any authored system code.
2. **User-defined extern systems**: developers can declare their own `extern system` with a custom filter and provide the C++ implementation. The backend generates the typed iteration scaffold; the developer fills in the body.

## What Changes

- Add `extern system Name:` as a new top-level declaration form
- An `extern system` has the same clauses as a regular `system` (`filter:`, `exclude:`, `order by:`, `after:`) but NO event handlers
- For stdlib-known patterns (filter contains recognized backend-driven traits like `Renderer`, `AnimatedSprite`, `PointLight`, etc.), the backend generates a complete optimized implementation
- For user-defined `extern system`, the backend generates a typed C++ iteration scaffold and a well-named callback signature that the user implements in their C++ game code
- The backend determines the best callback form (per-entity vs. batch) based on the filter — the author does not specify this
- Update the stdlib `std.render.*`, `std.physics.*`, `std.audio.*` modules to use `extern system` instead of leaving passive traits unconnected

## Capabilities

### New Capabilities
- `dsl-extern-system`: The `extern system` declaration syntax, semantics, and backend codegen contract

### Modified Capabilities
- `dsl-parser`: New `extern_system_decl` production
- `dsl-semantic-analysis`: Validation rules for extern system declarations (filter required, no handlers, name uniqueness)
- `backend-cpp-entt`: Recognition of stdlib-known patterns + C++ scaffold generation for user-defined extern systems
- `stdlib-render`: Update `std.render.sprites` and `std.render.meshes` to declare `extern system` for their passive traits

## Impact

- `src/frontend/ast.h`: Add `ExternSystemNode` struct; add to `Declaration` variant
- `src/frontend/lexer.cpp/h`: `extern system` is already tokenized (`extern` keyword exists); add recognition of the system-following `extern` context
- `src/frontend/parser.cpp/h`: New `parseExternSystemDecl()` method
- `src/frontend/semantic_analyzer.cpp/h`: Validate extern system declarations
- `src/backends/cpp-entt/system_emitter.cpp`: Generate implementations for known patterns and scaffolds for user-defined extern systems
- `stdlib/std/render/sprites.cactus`: Add `extern system SpriteRenderer` and `extern system AnimatedSpriteSystem`
- `stdlib/std/render/meshes.cactus`: Add `extern system MeshRenderer`, `extern system PointLightSystem`, etc.
