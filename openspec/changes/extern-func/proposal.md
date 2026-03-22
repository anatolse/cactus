## Why

The Cactus stdlib modules (`std.math`, `std.math.vec2/vec3/quat`, `std.input`, and the math/input/render functions to come) declare free functions whose bodies are **not implemented in Cactus** — they are provided by the backend runtime (Raylib wrappers, platform API calls, etc.). Currently there is no syntactic mechanism to express this, which causes two concrete problems:

1. **`std.math.*` functions are broken** — they are written as body-less `pub func lerp(a, b, t: float) -> float` (no colon, no body). The parser unconditionally requires `consume(':')` then `parse_block()`, so these files currently **fail to parse**.

2. **`std.input` functions lie** — because body-less is unrepresentable, `pressed`, `down`, `released`, `axis`, and `axis2` carry dummy bodies (`return false`, `return 0.0`). These are not real implementations; they exist only to satisfy the parser. The bodies would be silently emitted by a backend as dead code.

Both backends (`cpp-entt`, `cpp-manual`) emit nothing for `FuncNode` at all — the backend has no way to distinguish "user-defined function with a real body" from "stdlib extern function whose body is provided by the runtime."

The fix is a first-class `extern func` declaration: a named function with a known signature whose implementation is provided by the backend, analogous to `extern` in C/C++.

## What Changes

### New syntax: `extern func`

```cactus
# Body-less, backend-provided:
pub extern func lerp(a, b, t: float) float
pub extern func pressed(b: InputButton) bool
pub extern func draw_sprite(tex: texture_id, pos: vec2, size: vec2) void
```

`extern func` is valid at module top-level, with or without `pub`. It has no colon and no body. It introduces the function's name and signature into the module's symbol table.

### Removal of `->` return type arrow from func syntax

The `->` arrow token is removed from all function declarations — both `func` (user-defined) and `extern func` (backend-provided). The return type, if present, follows the closing `)` directly, separated by a space:

```cactus
# Before:
pub func clamp(x, lo, hi: float) -> float:
    ...
pub extern func sin(a: float) -> float

# After:
pub func clamp(x, lo, hi: float) float:
    ...
pub extern func sin(a: float) float
```

This applies to all `func` and `extern func` declarations. Functions with no return type are unchanged (the return type is simply absent).

### All stdlib math/input/render free functions use `extern func`

- `std.math` — all scalar functions (`lerp`, `clamp`, `abs`, `sin`, `cos`, etc.)
- `std.math.vec2/vec3/quat` — all vector/quaternion functions
- `std.input` — all query functions (`pressed`, `down`, `released`, `axis`, `axis2`) replace their dummy bodies with `extern`
- `std.render.*` free functions (when added) — likewise `extern`

### Compiler pipeline changes

The change touches every layer of the compiler:

**Lexer** — new `EXTERN` token keyword.

**AST** (`FuncNode`) — new `bool is_extern = false` flag. When `true`, the `body` vector is always empty and no colon is expected.

**Parser** — handle `pub extern func` and `extern func` at declaration level. When `is_extern == true`, parse only the signature (name, params, optional `-> type`), no colon, no block.

**Semantic analyzer** — three adjustments:
- `check_func_purity`: skip extern funcs (no body to check)
- `check_no_recursion`: skip extern funcs (no call graph entries)
- Phase 2 / decorated output: populate a new `result_.funcs` map with `ResolvedFunc` entries so backends can see extern functions

**`DecoratedProgram`** — new field: `std::unordered_map<std::string, ResolvedFunc> funcs`

**`ImportedSymbols`** — new field: `std::unordered_map<std::string, ResolvedFunc> funcs` so extern funcs from stdlib are visible to importing modules

**`ModuleArtifact`** — new `funcs` section in the `.cmod` binary format (serializes `ResolvedFunc` entries)

**Backends** — when the `DecoratedProgram` contains any extern func (i.e., `program.funcs` is non-empty or any imported module's funcs are extern), the backend emits:

```cpp
#include "cactus_runtime.h"   // provides all extern func implementations
```

The backend does **not** emit a C++ function body for extern funcs — the runtime header is hand-authored per backend and provides the actual implementations (e.g., wrapping Raylib math, `IsKeyDown`, etc.).

### New `ResolvedFunc` type

```cpp
struct ResolvedParam {
    std::string name;
    TypeInfo    type;
};

struct ResolvedFunc {
    std::string                name;
    bool                       is_pub    = false;
    bool                       is_extern = false;
    std::vector<ResolvedParam> params;
    std::optional<TypeInfo>    return_type;
};
```

Added to `DecoratedProgram`, `ImportedSymbols`, and `ModuleArtifact` serialization.

## Capabilities

### New Capabilities

- `dsl-extern-func`: the `extern func` declaration form — syntax, semantics, and artifact representation

### Modified Capabilities

- `dsl-lexer`: new `EXTERN` keyword token
- `dsl-parser`: `extern func` declaration parsing; `FuncNode.is_extern` flag; body-less parse path
- `dsl-semantic-analysis`: skip purity/recursion checks for extern funcs; populate `DecoratedProgram.funcs`
- `module-artifact`: new `funcs` section in `.cmod` format
- `backend-cpp-entt`: emit `#include "cactus_runtime.h"` when extern funcs are present; skip body emission for extern funcs
- `backend-cpp-manual`: same as cpp-entt

### Modified stdlib files

- `stdlib/std/math.cactus` — change all `pub func` to `pub extern func`
- `stdlib/std/math/vec2.cactus` — same
- `stdlib/std/math/vec3.cactus` — same
- `stdlib/std/math/quat.cactus` — same
- `stdlib/std/input.cactus` — change all `pub func ... : body` to `pub extern func` (remove dummy bodies)

## Impact

- **No game-code changes required** — call sites like `m.lerp(a, b, t)` are unaffected; the change is in how the declaration is written in stdlib, not how it is called
- **Breaking: `std.input` dummy bodies removed** — any code that somehow depended on `pressed()` returning `false` at compile time is affected, but that was always incorrect behavior
- **Parser is backward-compatible** — `func` without `extern` still requires a body (no change to existing user-defined functions)
- **`cactus_runtime.h` must be authored per backend** — this is a backend responsibility; the compiler change just ensures the `#include` is emitted when needed
- **`.cmod` format version bump** — the new `funcs` section requires bumping `CURRENT_VERSION` in `ModuleArtifact`
