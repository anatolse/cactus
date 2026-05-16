## 1. Audit examples

- [x] 1.1 Inventory current examples and identify syntax/comment drift against the current spec and implementation.
- [x] 1.2 Prioritize examples already referenced by curated compilation tests.
- [x] 1.3 Record any discovered spec-vs-implementation disagreements for follow-up decisions.

## 2. Fix examples in place

- [x] 2.1 Update examples to use only current block-structured `add`, `emit`, and `spawn` syntax.
- [ ] 2.2 Update examples to use `alias.field` or `TraitName.field` access in system handlers.
- [x] 2.3 Remove or reword stale comments about removed `apply:`, `config:`, handler-parameter, or parenthesized dynamic-trait syntax.
- [x] 2.4 Rewrite or remove snippets that present unimplemented future syntax as current syntax.

## 3. Align validation

- [x] 3.1 Keep curated compilation tests pointed at the existing examples they validate unless a separate change intentionally changes coverage.
- [x] 3.2 Ensure failures identify the exact example and whether the failure is parse, semantic, codegen, compile, format, or link related.
- [x] 3.3 Add or improve lightweight drift checks for examples not fully covered by compilation tests, if practical.

## 4. Review and documentation

- [x] 4.1 Update README or example documentation if it references stale syntax.
- [x] 4.2 Ensure examples continue to use their existing paths unless path changes are explicitly justified elsewhere.
- [x] 4.3 Run relevant parser, semantic, and example compilation tests.

## Implementation notes

- 1.3 / 2.2 blocker: Current specs require `alias.field` or `TraitName.field` field access in system handlers, but the current parser rejects assignment targets such as `p.x = p.x + tick.dt` with `expected newline`. Curated examples currently rely on bare assignment statements to keep compiling, so fully completing 2.2 requires either compiler support for member assignment targets or an artifact decision to keep examples implementation-aligned until that support lands.
