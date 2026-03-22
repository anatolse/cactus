## Context

The Cactus DSL has a `func` declaration that requires a body: `func name(params) -> type: INDENT body DEDENT`. The stdlib modules `std.math.*` and `std.input` need free functions whose implementations are provided by the backend C++ runtime (Raylib wrappers, platform APIs), not written in Cactus. Today these modules work around the constraint with either body-less declarations (which fail to parse) or dummy stub bodies (which compile to incorrect C++ dead code). Neither approach is correct.

The `interface` node already parses body-less method signatures for structural typing. The pattern is already understood in the codebase — extern functions extend it to top-level `func` declarations.

Neither backend emits any output for `FuncNode` today. `DecoratedProgram` tracks `func_names_` only as a name set; there is no `ResolvedFunc` in the decorated output, making funcs entirely invisible past the semantic phase.

## Goals / Non-Goals

**Goals:**
- Add `extern func` syntax: a top-level function declaration with no body, semantically meaning "implementation is provided by the backend runtime"
- Fix all stdlib math/input free functions to use `extern func`
- Propagate extern funcs through the full pipeline: AST → semantic → artifact → both backends
- Backends emit `#include "cactus_runtime.h"` when extern funcs are present in scope
- Maintain full backward compatibility for user-defined `func` (body required, purity enforced)

**Non-Goals:**
- `cactus_runtime.h` implementation (backend runtime authoring is separate work)
- Extern for other declaration kinds (traits, events, units)
- FFI beyond the backend runtime contract (no arbitrary C symbol linking)
- Recursion or purity analysis for extern funcs (trivially inapplicable — no body)

## Decisions

### D1: `extern func` keyword pair (not body-less func)

**Decision:** Use `pub extern func name(params) -> type` with no colon, no body. The `extern` keyword explicitly marks backend-provided functions.

**Rationale:** Body-less `func` (no colon) looks like a parse error to readers unfamiliar with the convention. The `extern` keyword is a clear, established signal borrowed from C/C++. It makes `grep extern` a reliable way to find all backend-contract functions. The `interface` node already solves structural typing with body-less signatures — this is a different concept (specific named functions, not polymorphism).

**Alternative considered:** Body-less func is implicitly extern (what std.math currently aspires to). Rejected — too implicit, visually ambiguous.

**Alternative considered:** `func ... = extern` suffix. Rejected — non-idiomatic to the language, reads awkwardly.

---

### D2: `ResolvedFunc` added to `DecoratedProgram` and `ImportedSymbols`

**Decision:** Add `std::unordered_map<std::string, ResolvedFunc> funcs` to both `DecoratedProgram` and `ImportedSymbols`. The semantic analyzer populates this map for all `func` declarations (extern and non-extern alike).

**Rationale:** Backends need to know which funcs are extern to decide whether to emit a body or just a `#include`. Imported extern funcs from stdlib modules must be visible in the importing compilation unit's decorated program so the backend knows the runtime header is needed.

**Alternative considered:** Keep funcs invisible to backends (status quo). Rejected — backends can't make correct codegen decisions without knowing about extern funcs.

---

### D3: Backend emits `#include "cactus_runtime.h"` when extern funcs are present

**Decision:** Both cpp-entt and cpp-manual backends inspect `program.funcs` (and imported module funcs via `program.ast`'s `use` declarations). If any extern func is in scope, they emit `#include "cactus_runtime.h"` at the top of the generated file.

**Rationale:** A single runtime header per backend is the simplest contract. The backend author writes the header; the compiler ensures it is included when needed. This avoids per-function `extern "C"` declarations in generated code, which would require the compiler to know the C++ name mangling.

**Alternative considered:** Emit per-function `extern` declarations. Rejected — requires the compiler to know exact C++ signatures including calling conventions, which couples compiler to backend implementation.

---

### D4: `.cmod` version bump for funcs section

**Decision:** Increment `ModuleArtifact::CURRENT_VERSION` from 1 to 2 when adding the funcs section. Loading a v1 artifact SHALL be rejected with a version mismatch error.

**Rationale:** The funcs section is a new binary section; v1 readers cannot skip it correctly. A hard version bump forces recompilation of all modules, which is the safe approach during this early stage of the compiler.

**Alternative considered:** Make funcs section optional (version stays at 1). Rejected — too fragile; old readers would silently misparse the new section as trailing garbage.

---

### D5: User-defined `func` retains purity requirement; extern func is exempt

**Decision:** The `check_func_purity` pass skips extern funcs (`fn->is_extern == true`). The `check_no_recursion` pass also skips extern funcs (no call graph entries from a body-less func).

**Rationale:** Purity is a Cactus guarantee over Cactus code. Extern funcs are backend code — we can't inspect or enforce purity over them. The stdlib contract is that extern funcs are pure (no side effects beyond their return value), but this is a documentation/convention constraint, not a compiler-enforced one.

## Risks / Trade-offs

**[Risk] `cactus_runtime.h` header doesn't exist yet**
→ Mitigation: The compiler correctly generates `#include "cactus_runtime.h"` as a forward reference. Generated code will fail to compile until the runtime header is authored. This is expected — the change ships the compiler side; runtime authoring is a follow-on task documented in the proposal.

**[Risk] `.cmod` version bump invalidates all cached artifacts**
→ Mitigation: During this stage of the project, cached artifacts are rebuilds from source. The bump is correct and necessary. Document in release notes.

**[Risk] User-authored `extern func` calls an undefined symbol**
→ Mitigation: The C++ linker will catch this. The compiler has no way to verify that the runtime header provides the promised symbol. This is the standard extern contract risk.

**[Risk] Stdlib files with dummy bodies (`std.input`) get silently replaced**
→ Mitigation: The proposal is explicit that `std.input`'s dummy bodies are always-wrong behavior. Removing them is a correctness fix. Any downstream test that expected `pressed()` to return `false` was testing the wrong thing.

---

### D6: Remove `->` return type arrow from all `func` declarations

**Decision:** The `->` arrow is dropped from `func` and `extern func` signatures. The return type immediately follows the closing `)`:

```
# old
pub func lerp(a, b, t: float) -> float:
pub extern func sin(x: float) -> float

# new
pub func lerp(a, b, t: float) float:
pub extern func sin(x: float) float
```

**Rationale:** The `->` is redundant punctuation — the language already has strict grammar and the parser can unambiguously determine "return type follows `)` and precedes `:` or NEWLINE". Removing it is consistent with how the rest of Cactus reads: fields use `name: type`, systems have `filter:` blocks, and nothing else uses `->`. The `->` was borrowed from Rust/Swift where it distinguishes return types from map/lambda syntax — Cactus has `=>` for lambdas, so the disambiguation is not needed. Dropping `->` also benefits stdlib files where the return type line is already long.

**Alternative considered:** Keep `->` (status quo). Rejected — adds noise for zero disambiguation benefit in this language.

**Impact:** All existing `.cactus` files that declare `func` with `-> type` must remove `->`. The `spec/cactus_dsl_spec.md` and other documentation must be updated. The parser change is trivial: drop the `consume(ARROW)` call in `parse_func_decl`. The `ARROW` token itself remains in the lexer for other uses (closures, type annotations elsewhere).

## Open Questions

- **What does `cactus_runtime.h` look like for the Raylib backend?** Needs to be scoped separately. At minimum it provides `lerp`, `clamp`, `abs`, `min`, `max`, `sqrt`, `sin`, `cos`, `atan2`, `floor`, `ceil`, `round`, `pow`, `pressed`, `down`, `released`, `axis`, `axis2`, and the vec2/vec3/quat function set.
- **Should `extern func` be restricted to stdlib modules only?** Currently no restriction — any module can declare `extern func`. This is intentional for user C++ interop. If abuse is a concern, a future attribute or module-level `extern` block could restrict it.
