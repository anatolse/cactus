# Proposal 007: Grouped Entity Instances

Status: draft  
Kind: scene declaration sugar  
Semantic change: none after expansion  
Recommended dependency: proposal 006

## Summary

Allow multiple named load-time entities based on one template to be declared in
one data-oriented block.

This proposal targets authored scene content such as gems, platforms, trees,
lights, and markers. It does not create a runtime collection or loop.

## Proposed syntax

With a parameterized template:

```cactus
entities from BubbleAt:
    Bubble1:
        position = vec2(250.0, 200.0)
        velocity = vec2(120.0, 90.0)
        radius = 12.0
        color = #FF6B6BFF

    Bubble2:
        position = vec2(400.0, 200.0)
        velocity = vec2(-100.0, 130.0)
        radius = 16.0
        color = #4ECDC4FF
```

Each direct assignment supplies a named template argument. An optional
`overrides:` block applies ordinary trait or child overrides:

```cactus
entities from Enemy(
    health = DEFAULT_HEALTH,
    color = DEFAULT_COLOR
):
    Guard1:
        position = vec2(300.0, 560.0)

    Guard2:
        position = vec2(800.0, 560.0)
        health = 5
        overrides:
            PatrolMotion:
                min_x = 700.0
                max_x = 1000.0
```

If proposal 006 is not implemented, grouped instances may contain only an
`overrides:` body. The feature is still useful but less compact.

## Grammar sketch

```ebnf
entity_group = [ "pub" ] "entities" "from" template_ref ":"
               NEWLINE INDENT
               group_instance+
               DEDENT ;

group_instance = IDENTIFIER ":" NEWLINE INDENT
                 { template_arg_assignment }
                 [ overrides_block ]
                 DEDENT ;
```

## Semantics and expansion

The group expands in source order to ordinary declarations:

```cactus
entity Bubble1 from BubbleAt(...):
entity Bubble2 from BubbleAt(...):
```

Consequently:

- every instance has its own global entity declaration identity;
- creation order equals group order;
- each instance participates independently in filters and events;
- persistence, hierarchy creation, and scene unloading are unchanged;
- the group itself has no runtime identity.

## Visibility

Recommended rule: `pub entities from ...` makes every contained instance
public. Per-instance visibility modifiers are not supported initially. This
keeps the block regular and easy to scan.

## Restrictions

- Instances must have unique names in the module.
- No computed iteration or dynamic number of instances.
- No anonymous rows.
- No spread from runtime lists or data files in the initial proposal.
- The block is for load-time entities only; runtime creation remains `spawn`.

## Compatibility

Ordinary `entity Name from Template:` remains canonical and fully supported.
The formatter should not automatically group unrelated declarations because
comments and deliberate ordering boundaries may carry meaning.

## Implementation work

- Parser: entity group and per-instance bodies.
- Semantic analysis: validate template arguments and module-level name clashes.
- Desugaring: expand to existing entity declarations before linking.
- Module artifacts: either serialize expanded declarations or add a source-only
  group node that never crosses the artifact boundary.
- Formatter: preserve authored grouping.
- Tests: order, public visibility, parameters, overrides, hierarchies, errors.

## Acceptance criteria

- A group of six instances produces the same six entities as hand-written
  declarations in the same order.
- Every instance name is addressable exactly as before.
- Duplicate instance names are rejected.
- The group creates no runtime list, entity, trait, or execution-graph node.
- Expansion is deterministic across module artifact round-trips.

## CIR impact

None. Expand groups to ordinary named entity declarations before CIR creation.

