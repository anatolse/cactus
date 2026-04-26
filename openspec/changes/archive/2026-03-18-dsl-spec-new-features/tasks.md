## 1. DSL Spec Document Updates

- [x] 1.1 Add `fixed_tick` and `late_tick` to the keyword list in `spec/cactus_dsl_spec.md`
- [x] 1.2 Add `asset` and `input` to the keyword list and `declaration` EBNF production
- [x] 1.3 Add the `asset_decl` EBNF production and `asset_type` enumeration to the grammar section
- [x] 1.4 Add the `input_decl` and `input_prop` EBNF productions to the grammar section
- [x] 1.5 Extend the `event_name` production to include `fixed_tick`, `late_tick`, and `input`
- [x] 1.6 Add Section: Four-phase per-frame update model (execution order diagram, phase descriptions)
- [x] 1.7 Add Section: Asset declarations (syntax, opaque ID types, path string exception)
- [x] 1.8 Add Section: Input declarations (syntax, button/axis kinds, property keys, std.input API)
- [x] 1.9 Extend Section 5.1 (built-in types) with `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id`, `InputButton`, `InputAxis`
- [x] 1.10 Update Section 6.1 (const-string rule) to document the asset path string exception

## 2. Lexer Changes

- [x] 2.1 Add `asset`, `input`, `fixed_tick`, `late_tick` as reserved keywords in the lexer token table (`src/frontend/lexer.cpp` / `token.hpp`)
- [x] 2.2 Add corresponding `TOKEN_ASSET`, `TOKEN_INPUT`, `TOKEN_FIXED_TICK`, `TOKEN_LATE_TICK` token types (or reuse IDENTIFIER with keyword lookup)
- [x] 2.3 Add lexer tests for the four new keywords

## 3. AST Node Additions

- [x] 3.1 Add `AssetDecl` AST node to `src/frontend/ast.hpp` (fields: `is_pub`, `name`, `asset_type`, `path`)
- [x] 3.2 Add `InputDecl` AST node to `src/frontend/ast.hpp` (fields: `is_pub`, `name`, `kind`, `props: list<InputProp>`)
- [x] 3.3 Add `InputProp` AST node (fields: `key`, `value: Expr`)
- [x] 3.4 Extend `EventHandler` AST node or `event_name` enum to represent `fixed_tick`, `late_tick`, `input`

## 4. Parser Changes

- [x] 4.1 Add `parse_asset_decl()` to handle `[pub] asset IDENTIFIER : asset_type = STRING_LITERAL`
- [x] 4.2 Add `parse_input_decl()` to handle `[pub] input IDENTIFIER : button|axis NEWLINE INDENT {input_prop} DEDENT`
- [x] 4.3 Register `asset` and `input` in the top-level declaration dispatch (alongside `system`, `trait`, etc.)
- [x] 4.4 Extend `event_name` parsing to accept `fixed_tick` and `late_tick` tokens
- [x] 4.5 Enforce that `on input():` takes no parameters (parse error if param list non-empty)
- [x] 4.6 Enforce that `on fixed_tick(dt: float):` and `on late_tick(dt: float):` require exactly one parameter
- [x] 4.7 Reject `asset_decl` and `input_decl` inside nested scopes (system, trait, func bodies)
- [x] 4.8 Add parser tests for `asset_decl` (all six asset types, pub variant, error cases)
- [x] 4.9 Add parser tests for `input_decl` (button with key/mouse/gamepad, axis with negative/positive/invert)
- [x] 4.10 Add parser tests for `on fixed_tick`, `on late_tick`, `on input()` handlers

## 5. Type System Changes

- [x] 5.1 Register `mesh_id`, `texture_id`, `sound_id`, `music_id`, `font_id`, `material_id` as built-in opaque types
- [x] 5.2 Register `InputButton` and `InputAxis` as built-in opaque types
- [x] 5.3 Add type-check rule: opaque asset ID types cannot be constructed directly (only from asset declarations)
- [x] 5.4 Add type-check rule: `InputButton`/`InputAxis` can only be assigned from resolved input declaration names

## 6. Semantic Analyzer Changes

- [x] 6.1 Register asset declarations in the module symbol table (name → opaque ID type based on asset_type)
- [x] 6.2 Resolve asset names used in `unit` config blocks to their opaque ID types; report type mismatches
- [x] 6.3 Enforce asset path string literals are only permitted inside `asset` declarations; reject all other out-of-const string literals
- [x] 6.4 Register input declarations in the module symbol table (name → `InputButton` or `InputAxis`)
- [x] 6.5 Validate `input_prop` keys against allowed set for button vs. axis kind
- [x] 6.6 Validate input property values reference known `std.input` enum constants (`Key.*`, `MouseButton.*`, `GamepadButton.*`, `GamepadAxis.*`)
- [x] 6.7 Add semantic tests for asset declaration resolution and type checking
- [x] 6.8 Add semantic tests for input declaration property validation

## 7. stdlib: std/input.cactus

- [x] 7.1 Create `stdlib/std/input.cactus` with `enum Key` (A–Z, Space, Escape, arrow keys, F1–F12, etc.)
- [x] 7.2 Add `enum MouseButton` (Left, Right, Middle) to `stdlib/std/input.cactus`
- [x] 7.3 Add `enum GamepadButton` (South, North, East, West, L1, R1, Start, Select) to `stdlib/std/input.cactus`
- [x] 7.4 Add `enum GamepadAxis` (LeftX, LeftY, RightX, RightY, TriggerL, TriggerR) to `stdlib/std/input.cactus`
- [x] 7.5 Add `pub func pressed(b: InputButton) -> bool` signature to `stdlib/std/input.cactus`
- [x] 7.6 Add `pub func down(b: InputButton) -> bool`, `pub func released(b: InputButton) -> bool` to `stdlib/std/input.cactus`
- [x] 7.7 Add `pub func axis(a: InputAxis) -> float` and `pub func axis2(x: InputAxis, y: InputAxis) -> vec2` to `stdlib/std/input.cactus`

## 8. Platformer Example Updates

- [x] 8.1 Update `examples/platformer/player.cactus`: replace stub input calls with `input.axis2(MoveX, MoveY)` and `input.pressed(Jump)` in `on input():` handler; replace per-frame movement with `on fixed_tick(dt: float):`
- [x] 8.2 Update `examples/platformer/camera.cactus`: move camera follow logic from `on tick()` to `on late_tick(dt: float):`
- [x] 8.3 Update `examples/platformer/platformer.cactus` (or main): add `asset` declarations for player sprite, enemy sprite, gem sprite
- [x] 8.4 Add `use std.input` and input action declarations (`MoveX`, `MoveY`, `Jump`, `Fire`, etc.) to relevant platformer files
- [x] 8.5 Verify all updated platformer `.cactus` files parse and pass semantic analysis with the new features
