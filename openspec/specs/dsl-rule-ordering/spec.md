## Purpose

Define the `after:` clause on rule declarations for expressing execution ordering, including rule-name resolution, cycle detection, and representation in the `DecoratedProgram` dependency graph.

## Requirements

### Requirement: `after:` clause on rule declarations
A rule-level `after:` clause SHALL be compatibility shorthand over handler nodes. For each named predecessor rule, it SHALL order only pairs of handlers with the same resolved phase or event trigger. A handler MAY additionally declare a leading `after:` block for exact canonical handler dependencies.

#### Scenario: Rule with no `after:` clause is valid
- **WHEN** a `rule` declaration contains no `after:` block
- **THEN** the parser accepts it and `RuleNode.after_rules` is empty

#### Scenario: Rule with single `after:` entry is valid
- **WHEN** a rule has an `after:` block with one indented rule name
- **THEN** `RuleNode.after_rules` contains exactly that one name

#### Scenario: Rule with multiple `after:` entries is valid
- **WHEN** a rule has an `after:` block listing `RuleA`, `RuleB`, `RuleC` on separate lines
- **THEN** `RuleNode.after_rules` contains `["RuleA", "RuleB", "RuleC"]`

#### Scenario: `after:` appears after `filter:` and `exclude:` and before handlers
- **WHEN** a rule body has `filter:`, then `exclude:`, then `after:`, then `on tick():`
- **THEN** the parser accepts the ordering and populates all clauses correctly

#### Scenario: Matching phase handlers are ordered
- **WHEN** B is after A and both rules handle tick
- **THEN** B.tick executes after A.tick

#### Scenario: Different triggers do not receive an edge
- **WHEN** A handles tick and B handles Damaged
- **THEN** rule-level `B after A` does not create a cross-trigger edge

#### Scenario: Precise handler dependency is accepted
- **WHEN** B.tick explicitly lists A.tick in its handler `after:` block
- **THEN** the handler graph contains that exact edge

### Requirement: `after:` rule name resolution
The semantic analyzer SHALL verify that every identifier listed in an `after:` clause resolves to a declared `rule` in the current compiled program (all linked modules). Referencing a non-existent rule name SHALL produce a compile error.

#### Scenario: Valid `after:` reference accepted
- **WHEN** `after: MovementRule` is declared and `rule MovementRule:` exists in the same or an imported module
- **THEN** the semantic analyzer accepts the reference and adds the ordering edge to the dependency graph

#### Scenario: Unknown rule name in `after:` rejected
- **WHEN** `after: NonExistentRule` is declared and no rule with that name exists
- **THEN** the semantic analyzer reports an error: "unknown rule 'NonExistentRule' in after clause"

#### Scenario: `after:` cannot reference a non-rule declaration
- **WHEN** `after: Position` is declared and `Position` is a trait, not a rule
- **THEN** the semantic analyzer reports an error: "'Position' is not a rule"

### Requirement: `after:` ordering cycle detection
The semantic analyzer SHALL detect cycles after expanding rule shorthand and combining explicit handler ordering with inferred handler conflict edges. Diagnostics SHALL identify the canonical handler-node cycle.

#### Scenario: Direct cycle detected
- **WHEN** `rule A:` declares `after: B` and `rule B:` declares `after: A`
- **THEN** the semantic analyzer reports an error: "cycle in rule ordering: A → B → A"

#### Scenario: Indirect (transitive) cycle detected
- **WHEN** `rule A: after: B`, `rule B: after: C`, `rule C: after: A`
- **THEN** the semantic analyzer reports an error that identifies the cycle path

#### Scenario: Linear chain with no cycle is valid
- **WHEN** `rule C: after: B` and `rule B: after: A` with no back-edges
- **THEN** the semantic analyzer accepts the declarations and the execution order is A → B → C

#### Scenario: Combined cycle is rejected
- **WHEN** explicit and inferred edges form A.tick -> B.tick -> A.tick
- **THEN** semantic analysis reports the handler-level cycle path

### Requirement: `after:` edges stored in DecoratedProgram dependency graph
The semantic analyzer SHALL store validated `after:` ordering constraints in the `RuleInfo` structure inside the `DecoratedProgram`. Each rule's `RuleInfo` SHALL include a list of rule names that it must follow.

#### Scenario: Ordering edges visible in DecoratedProgram
- **WHEN** `rule UIRenderRule: after: SceneRenderRule` is compiled
- **THEN** the `DecoratedProgram` contains `UIRenderRule.after_rules = ["SceneRenderRule"]`
