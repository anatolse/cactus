# Proposal 003: First-Class `else if` Chains

Status: implemented (`b5412d3`); normative grammar in `dsl-parser` and spec §3.16  
Kind: parser sugar  
Semantic change: none

## Summary

Allow `else if` at the same indentation level to avoid a staircase of nested
`else:` and `if` blocks.

## Proposed syntax

```cactus
if health <= 0:
    destroy
else if health < 25:
    state = EnemyState.Fleeing
else if health < 50:
    state = EnemyState.Defensive
else:
    state = EnemyState.Attacking
```

The proposal uses two existing words rather than adding an `elif` keyword.

## Grammar sketch

```ebnf
if_stmt = "if" expression ":" suite
          { "else" "if" expression ":" suite }
          [ "else" ":" suite ] ;
```

Newline and indentation handling should match the existing `else:` clause.

## Semantics

Conditions are evaluated from top to bottom. Exactly the first true branch
executes. The final `else` is optional.

The parser may lower:

```cactus
else if condition:
```

to an ordinary nested `IfStmt` stored in the previous `else` body. Semantic
analysis and code generation therefore require no new node kind.

## Diagnostics

Reject:

- `else if` after a terminal `else`;
- more than one terminal `else`;
- a misindented `else if` that does not align with its owning `if`;
- an empty `else if` suite.

## Compatibility

Existing nested `else:` + `if` source remains valid. The formatter may rewrite
an `else` body containing exactly one `IfStmt` into `else if`, provided comments
and source locations can be preserved.

## Implementation work

- Lexer: no new token is required.
- Parser: recognize `ELSE IF` as a continuation of the same chain.
- AST: reuse nested `IfStmt` lowering or add an explicit branch list only if it
  materially improves diagnostics.
- Formatter: emit the flat spelling.
- Tests: parser, malformed indentation, comments, codegen equivalence.

## Acceptance criteria

- A three-branch `else if` chain parses and compiles.
- Its generated code is equivalent to the nested legacy spelling.
- Comments between branches remain attached to the intended branch.
- Existing `else:` behavior is unchanged.
- No CIR change is observable.

## CIR impact

None. Normalize to existing conditional branches before CIR construction.

