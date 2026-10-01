# Proposal 005: Dotted Child-Path Overrides

Status: draft  
Kind: hierarchical archetype sugar  
Semantic change: none

## Summary

Allow an instance or spawn site to override a deep descendant role without
repeating every intermediate `children:` block.

Declarations remain structurally nested because the nesting explains topology.
Only overrides gain a path form.

## Motivation

Current deep override:

```cactus
entity Tree1 from Tree:
    children:
        Crown:
            children:
                Gem:
                    Label:
                        text_id = 131
```

The repeated hierarchy does not add information: `Tree` already declared it.

## Proposed syntax

```cactus
entity Tree1 from Tree:
    override Crown.Gem:
        Label:
            text_id = 131
```

Multiple target paths may be overridden independently:

```cactus
entity Tree1 from Tree:
    override Crown:
        Growth:
            target_scale = 2.0

    override Crown.Gem.Sparkle:
        Growth:
            target_scale = 3.0
```

The same syntax is valid in a `spawn` body:

```cactus
let tree = spawn Tree:
    override Crown.Gem:
        Label:
            text_id = 200
```

## Grammar sketch

```ebnf
child_path_override = "override" IDENTIFIER { "." IDENTIFIER } ":"
                      NEWLINE INDENT
                      { archetype_trait_entry }
                      DEDENT ;
```

Paths contain child role names only. The trait overrides appear inside the
target body, so the compiler never has to guess where the role path ends and a
trait name begins.

## Semantics

1. Resolve the base template and flatten its role tree.
2. Starting at the root, resolve each path segment among immediate child roles.
3. Apply the override body to the final role using ordinary field-by-field
   archetype override rules.
4. Preserve the original declaration and creation order.

This is an authored-archetype operation. It does not provide runtime lookup of
an entity by role path and does not expose descendant entity handles.

## Conflicts and ordering

Recommended rule: duplicate writes to the same target trait field in one
archetype body are a compile-time error, regardless of whether one write came
from nested syntax and another from a path override.

For example, reject:

```cactus
entity Bad from Tree:
    children:
        Crown:
            Growth:
                target_scale = 2.0

    override Crown:
        Growth:
            target_scale = 3.0
```

This avoids source-order-dependent configuration.

## Diagnostics

Errors should include the resolved prefix and known child roles:

```text
unknown child role 'Gemm' in override path 'Crown.Gemm';
known children of 'Crown': Gem, LeafCluster
```

Also reject:

- an empty path;
- an override path on an archetype with no matching base hierarchy;
- a trait not present on the target role when ordinary override rules require
  pre-existence;
- duplicate target-field overrides.

## Compatibility

Existing nested override syntax remains valid. The formatter should not
automatically rewrite nested source when comments are attached to intermediate
roles.

## Implementation work

- Lexer: add or contextualize `override`.
- Parser: add `ChildPathOverride` to archetype bodies.
- Semantic analysis: resolve paths against the composed role tree.
- Template flattening: translate paths to ordinary nested role overrides.
- Diagnostics: expose sibling role suggestions.
- Tests: entity, spawn, composed templates, unknown segment, duplicate write.

## Acceptance criteria

- A three-segment override changes exactly the final descendant.
- Nested and path forms flatten identically.
- Immediate-parent `Parent` relations and preorder creation are unchanged.
- A path cannot be evaluated at runtime.
- Invalid paths fail before code generation.

## CIR impact

None. Resolve and apply path overrides before archetype lowering into CIR.

