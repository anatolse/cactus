## ADDED Requirements

### Requirement: `order by:` clause parsing in system declarations
The parser SHALL recognize an optional `order by:` block in system declarations, positioned between the `filter:`/`exclude:` clauses and the event handler list. The `order by:` block contains one or more sort key lines, each consisting of a dotted alias-field expression followed by an optional direction keyword.

```ebnf
system_decl     = "system" IDENTIFIER ":" INDENT
                  [filter_clause]
                  [exclude_clause]
                  [order_by_clause]
                  handler+
                  DEDENT ;

order_by_clause = "order" "by" ":" INDENT sort_key+ DEDENT ;
sort_key        = IDENTIFIER "." IDENTIFIER ["asc" | "desc"] NEWLINE ;
```

`order` and `by` are contextual keywords in this production. `asc` and `desc` are contextual direction keywords.

#### Scenario: order by clause with single key parsed
- **WHEN** a system contains `order by:` with one indented `s.layer asc` line
- **THEN** the parser produces a `SystemNode` with `order_by = [{alias="s", field="layer", descending=false}]`

#### Scenario: order by clause with multiple keys parsed
- **WHEN** a system contains `order by:` with `s.layer` then `p.pos.y desc`
- **THEN** the parser produces `order_by` with two entries: `{alias="s", field="layer", descending=false}` and `{alias="p", field="pos.y", descending=true}`

#### Scenario: order by with default asc direction
- **WHEN** a sort key line has no direction keyword
- **THEN** the parser produces a `SortKey` with `descending = false`

#### Scenario: system without order by has empty order_by
- **WHEN** a system declaration has no `order by:` block
- **THEN** `SystemNode.order_by` is an empty vector
