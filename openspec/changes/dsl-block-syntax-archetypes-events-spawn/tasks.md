## 1. AST and parser foundation

- [ ] 1.1 Replace `unit` and `template` AST shapes so they store nested trait entries with optional field assignment blocks instead of `ApplyBlock` and `ConfigBlock`
- [ ] 1.2 Replace flat `SpawnArg` and positional `EmitStmt` payload storage with nested block-based initialization structures in the AST
- [x] 1.3 Update lexer/parser grammar for `unit` and `template` bodies to parse marker traits and `Trait:` field blocks
- [x] 1.4 Update parser support for block-structured `spawn` in both expression and statement form
- [x] 1.5 Update parser support for block-structured `emit`, including optional `to <expr>` before the payload block

## 2. Semantic validation

- [ ] 2.1 Replace archetype alias/config-key validation with structural validation of nested trait blocks in units and templates
- [ ] 2.2 Validate block-structured `spawn` overrides against template trait membership and trait field definitions
- [ ] 2.3 Validate block-structured `emit` payload fields against declared event fields
- [ ] 2.4 Preserve targeted emit type-checking for `emit Event to entity_id:` and update diagnostics to match the new syntax

## 3. Tests and examples

- [x] 3.1 Update parser tests to cover nested unit/template trait blocks, block `spawn`, and block `emit`
- [x] 3.2 Update semantic tests to cover invalid trait fields, invalid spawn overrides, and invalid emit payload fields under the new syntax
- [ ] 3.3 Migrate showcase and example `.cactus` files from `apply:` / `config:` and parenthesized `spawn` / `emit` forms to the new block syntax

## 4. Cleanup and verification

- [ ] 4.1 Remove legacy archetype alias/config parsing and semantic resolution paths that are superseded by nested blocks
- [x] 4.2 Remove or adapt tests that assert old alias-qualified config/spawn behavior
- [ ] 4.3 Run the relevant parser and semantic test suites and fix any remaining regressions