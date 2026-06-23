## 1. Lexer, parser, and AST

- [x] 1.1 Add an `entity` keyword/token and migrate the active declaration keyword from `unit` to `entity`.
- [x] 1.2 Update AST naming from unit declarations to entity declarations while preserving declaration name, pub flag, and archetype body data.
- [x] 1.3 Add an optional `from dotted_name` template reference to entity declarations.
- [x] 1.4 Update top-level declaration parsing to accept `entity Name:` and `entity Name from TemplateName:` declarations and reject legacy `unit` declarations.
- [x] 1.5 Add parser tests for inline entities, local template-backed entities, qualified template references, aliased template references, and legacy `unit` rejection.

## 2. Semantic analysis

- [x] 2.1 Rename semantic entity-instance handling from units to entities in symbols, diagnostics, and decorated program structures.
- [x] 2.2 Resolve each `entity ... from TemplateName` reference using existing module/import rules.
- [x] 2.3 Reject template-backed entities that reference undefined templates, non-template symbols, or private imported templates.
- [x] 2.4 Validate template-backed entity override trait entries against the referenced template and declared traits.
- [x] 2.5 Define and implement flattened template-backed entity archetype construction using template defaults plus entity overrides.
- [x] 2.6 Preserve deterministic declaration order for mixed inline and template-backed entities.
- [x] 2.7 Add semantic tests for valid overrides, unknown templates, non-template references, private imported templates, unknown fields, missing required fields, name-as-expression rejection, and deterministic declaration ordering.

## 3. Backend, data files, and artifacts

- [x] 3.1 Update module artifacts and data-file writing/reading terminology and serialization to use entity declarations instead of unit declarations.
- [x] 3.2 Update cpp-entt setup generation to instantiate inline entities and template-backed entities during module/world initialization.
- [x] 3.3 Ensure template-backed entity code uses the same component initialization paths as inline entities and spawn sites.
- [x] 3.4 Preserve deterministic mixed entity creation order in generated setup code.
- [x] 3.5 Add backend tests that generated template-backed entities contain template traits plus per-entity overrides.

## 4. Examples and docs

- [x] 4.1 Migrate existing examples, fixtures, and documentation from `unit` to `entity`.
- [x] 4.2 Add a small supported example showing templates plus `entity Name from Template:` declarations.
- [x] 4.3 Refactor a repetitive example section, such as platforms or gems, to use template-backed entities after the feature is implemented.
- [x] 4.4 Document the distinction between `entity`, `template`, `spawn`, and deferred grouped `entities from Template:` syntax.
- [ ] 4.5 Run lexer, parser, semantic, backend, artifact/data-file, and curated example compilation tests.
