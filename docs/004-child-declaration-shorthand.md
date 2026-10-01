# Proposal 004: Child Declaration Shorthand

Status: draft  
Kind: contextual parser sugar  
Semantic change: none

## Summary

Inside a `children:` block, make the repeated `entity` keyword optional and
allow a child with no local body to omit its colon and empty suite.

The hierarchy remains visibly nested because indentation represents parentage.
This proposal removes tokens that repeat information already supplied by the
`children:` context.

## Current syntax

```cactus
template Tree:
    TreeRoot

    children:
        entity Crown from TreeCrown:
            CrownTint:
                color = #44AA44FF

        entity Shadow from ShadowTemplate:
```

## Proposed syntax

```cactus
template Tree:
    TreeRoot

    children:
        Crown from TreeCrown:
            CrownTint:
                color = #44AA44FF

        Shadow from ShadowTemplate
```

Inline children remain possible:

```cactus
children:
    Crown:
        TreeCrown
        children:
            Gem from GemTemplate
```

The existing explicit spelling remains valid:

```cactus
children:
    entity Crown from TreeCrown:
        ...
```

## Contextual grammar sketch

```ebnf
child_decl = [ "entity" ] IDENTIFIER [ "from" dotted_name ]
             ( NEWLINE
             | ":" NEWLINE INDENT { archetype_entry } DEDENT ) ;
```

The shorthand is recognized only while parsing a `children:` block. It does not
change top-level entity declarations and does not reserve child role names.

## Semantics and lowering

Both spellings create the same hierarchical archetype node:

- role name remains sibling-scoped;
- `from` still splices the referenced template at compile time;
- generated `Parent` points to the immediate containing node;
- creation order remains parent-first preorder;
- creating the hierarchy still returns only the root entity.

A leaf without a body is equivalent to an empty override body.

## Readability rules

- Keep structural nesting when it communicates real parentage.
- Extract a repeated or conceptually independent subtree into a template.
- Do not flatten declarations into path strings merely to reduce indentation.
- Use proposal 005 only for deep overrides of an already-declared tree.

## Diagnostics

- Report duplicate sibling role names.
- Report a leaf shorthand for a child that lacks both a template and local
  traits only if empty child entities are otherwise forbidden.
- Preserve precise role-path diagnostics after shorthand lowering.

## Implementation work

- Extend contextual parsing inside `children:`.
- Normalize shorthand to the existing `ChildDecl` AST shape.
- Teach the formatter to prefer shorthand by default.
- Add parser and hierarchical-flattening equivalence tests.

## Acceptance criteria

- Explicit and shorthand declarations generate identical flattened archetypes.
- Leaf `Child from Template` requires no colon.
- Top-level `entity` syntax is unchanged.
- Nested role paths and generated `Parent` relations are unchanged.
- No new CIR node or runtime behavior is introduced.

## CIR impact

None. The shorthand is normalized during parsing and hierarchy flattening.

