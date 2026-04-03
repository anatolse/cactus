## Why

The current DSL uses a split archetype syntax for `unit` and `template` declarations: `apply:` declares traits, `config:` assigns fields, and aliases are needed to disambiguate dotted keys. This makes entity blueprints harder to read and pushes structural intent into name-resolution rules instead of expressing it directly in the source.

This change introduces a block-structured syntax for archetypes, event emission, and template spawning so authors can describe traits and payloads directly with nested blocks. The result is a more uniform language model, fewer qualification rules, and a syntax that better matches the indentation-based style of the rest of the DSL.

## What Changes

- **BREAKING**: Remove `apply:` and `config:` blocks from `unit` and `template` declarations.
- **BREAKING**: Remove `as` aliases from `unit` and `template` trait application syntax.
- Add nested trait block syntax for `unit` and `template` declarations, where each entry is either a marker trait or a trait followed by an indented field-assignment block.
- **BREAKING**: Remove alias-qualified and trait-qualified config keys in archetype declarations because field ownership is expressed structurally by nesting.
- Add block syntax for `spawn` overrides so template instantiation uses nested trait blocks instead of flat `spawn Foo(field = value)` argument lists.
- Add block syntax for `emit` payload construction so emitted events are initialized with indented field assignments instead of positional/named parenthesized arguments.
- Preserve a distinct targeted emit form so delivery target remains separate from event payload.
- Update examples, parser rules, semantic validation, and AST structures to model block-structured initialization consistently.

## Capabilities

### New Capabilities
- `dsl-block-structured-initializers`: A unified nested-block syntax for archetype trait initialization, spawn overrides, and event payload construction.

### Modified Capabilities
- `dsl-parser`: Change grammar for `unit`, `template`, `emit`, and `spawn` to support block-structured forms and remove archetype aliases/config qualification syntax.
- `dsl-semantic-analysis`: Replace alias/key-prefix-based resolution for archetype config and spawn overrides with structural validation of nested trait blocks and event payload blocks.
- `dsl-templates`: Change template declaration and spawn-site requirements to use nested trait blocks instead of `apply:`/`config:` and parenthesized override arguments.
- `dsl-unit-config-qualification`: Remove archetype aliasing and dotted config/spawn qualification requirements, superseded by nested trait blocks.

## Impact

- Affected frontend code: `src/frontend/ast.h`, `src/frontend/parser.h`, `src/frontend/parser.cpp`, `src/frontend/semantic_analyzer.h`, `src/frontend/semantic_analyzer.cpp`.
- Affected tests: parser and semantic tests covering units, templates, spawn, emit, and config qualification.
- Affected examples and docs: `examples/dsl_showcase.cactus`, other example `.cactus` files, and relevant DSL spec documents.
- This is a source-breaking syntax migration for authors using `apply:`, `config:`, archetype aliases, or parenthesized `spawn`/`emit` initialization forms.