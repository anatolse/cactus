## 1. Parser and AST

- [x] 1.1 Add an archetype template-use entry AST node that stores the referenced template name.
- [x] 1.2 Parse archetype-body `use TemplateName` entries inside template bodies.
- [x] 1.3 Parse archetype-body `use TemplateName` entries inside unit bodies.
- [x] 1.4 Add parser tests for local, qualified, and aliased archetype-body use names.

## 2. Semantic analysis

- [ ] 2.1 Resolve archetype-body `use` entries to template declarations using existing module/import rules.
- [ ] 2.2 Reject archetype-body `use` entries that reference non-template symbols.
- [ ] 2.3 Detect and report cyclic template-use graphs.
- [ ] 2.4 Flatten used template entries into composed archetypes using deterministic field-override semantics.
- [ ] 2.5 Add semantic tests for merge order, field overrides, marker duplicates, imported pub templates, ambiguous names, and cycles.

## 3. Backend/codegen

- [ ] 3.1 Update cpp-entt codegen to consume flattened composed archetypes for templates and units.
- [ ] 3.2 Ensure runtime `spawn` of a composed template constructs all traits from used templates exactly once.
- [ ] 3.3 Add backend tests for units and spawn sites based on composed templates.

## 4. Examples and docs

- [ ] 4.1 Add a small canonical example showing archetype-body `use`-based template reuse.
- [ ] 4.2 Update language docs/spec examples to explain composition versus runtime `spawn`.
- [ ] 4.3 Run parser, semantic, backend, and curated example compilation tests.
