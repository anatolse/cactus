# Proposal 002: Handler Control-Flow Readability Rules

Status: draft  
Kind: style guide and lint  
Semantic change: none

## Summary

Establish a canonical style for avoiding deeply nested executable control flow
inside handlers. Use combined conditions, early `return`, and value dispatch
instead of adding a general declarative `where:` clause.

## Motivation

Nested structural data can be readable because indentation shows topology.
Nested behavior is different: every additional `if` makes the reader retain
another runtime condition.

Problematic form:

```cactus
on input:
    if active:
        if not use_3d:
            if mode == GizmoMode.Place:
                if input.pressed(EditorSelectClick):
                    let position = camera2d.screen_to_world(input.mouse_position())
                    place(position)
```

## Canonical transformations

### Combine short independent conditions

```cactus
if active and not use_3d and mode == GizmoMode.Place:
    if input.pressed(EditorSelectClick):
        place_at_cursor()
```

Use this when all conditions are short, have no intermediate computation, and
form one conceptual predicate.

### Use early return for sequential gates

```cactus
on input:
    if not active or use_3d:
        return

    if mode != GizmoMode.Place:
        return

    if not input.pressed(EditorSelectClick):
        return

    let position = camera2d.screen_to_world(input.mouse_position())
    place(position)
```

`return` exits only the current handler invocation. For a unary handler, it
does not stop the rule for other selected entities. This behavior must remain
explicit in the language specification.

### Use value dispatch for mutually exclusive states

When value `match` is available, prefer it for enum dispatch:

```cactus
match mode:
    GizmoMode.Select =>
        select_at_cursor()
    GizmoMode.Place =>
        place_at_cursor()
    GizmoMode.Translate =>
        translate_selection()
    _ =>
        return
```

This is separate from trait matching on `entity_id`. Pair domains do not
replace either form of dispatch.

## Lint guidance

Recommended non-fatal diagnostics:

- nesting depth 3: advisory warning;
- nesting depth 4 or greater: stronger warning;
- `else:` whose only statement is another `if`: suggest `else if` after
  proposal 003 is implemented;
- repeated enum equality chain: suggest value `match` when supported.

The lint must count executable blocks, not declaration indentation. Nested
`children:` blocks are outside this rule.

## Non-goals

- No `where:` clause on a rule or pair domain.
- No new `guard`, `unless`, or assertion keyword.
- No change to handler activation, event delivery, or command commit timing.
- No hard compiler limit on nesting depth.

## Implementation work

- Document handler-local `return` precisely.
- Add AST nesting-depth analysis to the optional lint pass.
- Rewrite maintained examples, especially editor handlers, into the canonical
  form.
- Keep data-dependent gameplay conditions inside handlers.

## Acceptance criteria

- The style guide contains examples for combined conditions, early return, and
  enum dispatch.
- The lint distinguishes handler control flow from structural declarations.
- No maintained handler contains four nested `if` blocks without an explicit
  lint suppression and rationale.
- Generated code and CIR are unchanged after style-only rewrites.

## CIR impact

None. These are source-level rewrites using existing branch and return nodes.

