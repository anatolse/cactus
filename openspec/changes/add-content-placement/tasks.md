## 1. Parser and AST

- [ ] 1.1 Add a `PlaceDecl` AST node with name, template reference, and nested override trait entries.
- [ ] 1.2 Add `place` and `from` parsing for top-level placement declarations.
- [ ] 1.3 Update top-level declaration parsing to accept `place` declarations.
- [ ] 1.4 Add parser tests for local, qualified, and aliased template references in placements.

## 2. Semantic analysis

- [ ] 2.1 Resolve each placement's template reference using existing module/import rules.
- [ ] 2.2 Reject placements that reference non-template symbols or private imported templates.
- [ ] 2.3 Validate placement override trait entries against the referenced template and declared traits.
- [ ] 2.4 Define and implement flattened placement archetype construction.
- [ ] 2.5 Add semantic tests for valid overrides, unknown templates, non-template references, unknown fields, and deterministic declaration ordering.

## 3. Backend/codegen

- [ ] 3.1 Update cpp-entt setup generation to instantiate placements during module/world initialization.
- [ ] 3.2 Ensure placement code uses the same component initialization paths as units and spawn sites.
- [ ] 3.3 Preserve deterministic unit/placement creation order.
- [ ] 3.4 Add backend tests that generated placement entities contain template traits plus per-placement overrides.

## 4. Examples and docs

- [ ] 4.1 Add a small supported example showing templates plus placements.
- [ ] 4.2 Refactor a repetitive example section, such as platforms or gems, to use placement after the feature is implemented.
- [ ] 4.3 Document the distinction between `unit`, `template`, `spawn`, and `place`.
- [ ] 4.4 Run parser, semantic, backend, and curated example compilation tests.
