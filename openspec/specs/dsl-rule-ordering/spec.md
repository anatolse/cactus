## Purpose

Define the `after:` clause on rule declarations for expressing execution ordering, including rule-name resolution, cycle detection, and representation in the `DecoratedProgram` dependency graph.

## Requirements

### Requirement: `after:` clause on rule declarations
A rule-level `after:` clause SHALL be shorthand over handler nodes and is a supported, non-deprecated form. For each named predecessor rule, it SHALL order only pairs of handlers with the same resolved phase or event trigger. A handler MAY additionally declare a leading `after:` block for exact canonical handler dependencies.

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
The semantic analyzer SHALL verify that every identifier listed in an `after:` or `before:` clause resolves to a declared `rule` or `group` in the current compiled program (all linked modules). Referencing a non-existent name SHALL produce a compile error.

#### Scenario: Valid `after:` reference accepted
- **WHEN** `after: MovementRule` is declared and `rule MovementRule:` exists in the same or an imported module
- **THEN** the semantic analyzer accepts the reference and adds the ordering edge to the dependency graph

#### Scenario: Unknown rule name in `after:` rejected
- **WHEN** `after: NonExistentRule` is declared and no rule or group with that name exists
- **THEN** the semantic analyzer reports an error: "unknown rule 'NonExistentRule' in after clause"

#### Scenario: `after:` cannot reference a non-rule declaration
- **WHEN** `after: Position` is declared and `Position` is a trait, not a rule or group
- **THEN** the semantic analyzer reports an error: "'Position' is not a rule or group"

#### Scenario: Unknown name in `before:` rejected
- **WHEN** `before: NonExistentRule` is declared and no rule or group with that name exists
- **THEN** the semantic analyzer reports an error: "unknown rule 'NonExistentRule' in before clause"

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

### Requirement: Rule group declarations
A module SHALL be able to declare a rule group with a top-level `group Name:` declaration, optionally prefixed with `pub`. The body SHALL contain exactly one `phase:` entry naming a declared phase. A group SHALL have no runtime behavior; it only names an ordering anchor inside that phase. `group` SHALL be recognized contextually at the top-level declaration position and SHALL remain usable as an identifier elsewhere. A group name SHALL share the module's declaration namespace.

#### Scenario: Group declaration is accepted
- **WHEN** a module declares `pub group solve:` with body `phase: fixed_tick`
- **THEN** the program compiles and `solve` is a group bound to the `fixed_tick` phase

#### Scenario: Missing phase entry is rejected
- **WHEN** a group declaration body has no `phase:` entry
- **THEN** compilation fails with an error naming the group and the missing `phase:`

#### Scenario: Phase entry must name a phase
- **WHEN** a group declares `phase: Damaged` and `Damaged` is an event, not a phase
- **THEN** compilation fails with an error that `Damaged` is not a phase

#### Scenario: Duplicate declaration name is rejected
- **WHEN** a module declares both `group Move:` and `rule Move:`
- **THEN** compilation fails with a duplicate-declaration error

#### Scenario: `group` remains an ordinary identifier elsewhere
- **WHEN** a trait declares a field named `group`
- **THEN** the program compiles

### Requirement: Rule group membership
A rule or extern rule SHALL join a group with a `group:` clause naming one group. Only rules declared in the same module as the group SHALL be able to join it. The member rule's handler whose resolved trigger is the group's phase SHALL become the group's member handler. The rule's handlers for other triggers SHALL NOT be affected. A rule SHALL join at most one group.

#### Scenario: Member handler joins the group
- **WHEN** rule `MoveAndSlide` declares `group: solve`, `solve` is bound to `fixed_tick`, and the rule handles `fixed_tick`
- **THEN** `MoveAndSlide`'s `fixed_tick` handler is a member of `solve`

#### Scenario: Handlers for other triggers are not members
- **WHEN** a member rule of a `fixed_tick` group also handles `input`
- **THEN** its `input` handler is not a member of the group and gets no edge from group ordering

#### Scenario: Member without a handler for the group's phase is rejected
- **WHEN** a rule declares `group: solve`, `solve` is bound to `fixed_tick`, and the rule has no `fixed_tick` handler
- **THEN** compilation fails with an error naming the rule, the group, and the phase

#### Scenario: Joining another module's group is rejected
- **WHEN** a game module rule declares `group: phys.solve` and `solve` is declared in `std.physics.volume`
- **THEN** compilation fails with an error that only rules in the declaring module may join the group

#### Scenario: Joining more than one group is rejected
- **WHEN** a rule's `group:` clause names two groups
- **THEN** compilation fails with an error that a rule joins at most one group

#### Scenario: Unknown group is rejected
- **WHEN** a rule declares `group: missing` and no group named `missing` is visible
- **THEN** compilation fails with an unknown-group error

### Requirement: `before:` clause on rule declarations
A rule or extern rule SHALL accept a `before:` clause listing rule names and group names. It is the mirror of rule-level `after:`. For a listed rule, each of the declaring rule's handlers SHALL run before that rule's handler with the same resolved trigger, and no cross-trigger edge SHALL be created. For a listed group, the declaring rule's handler for the group's phase SHALL run before every member handler of the group. `before` SHALL be recognized contextually at the rule-clause position.

#### Scenario: Rule runs before a named rule
- **WHEN** rule A declares `before: B` and both rules handle `tick`
- **THEN** `A.tick` executes before `B.tick`

#### Scenario: `before:` creates no cross-trigger edge
- **WHEN** A declares `before: B`, A handles `tick`, and B handles only `Damaged`
- **THEN** no edge is created between A and B

#### Scenario: Rule runs before a group
- **WHEN** rule `MovePlayer` declares `before: phys.solve` and `phys.solve` is bound to `fixed_tick`
- **THEN** `MovePlayer.fixed_tick` executes before every member handler of `phys.solve`

#### Scenario: Rule cannot be before itself
- **WHEN** rule A declares `before: A`
- **THEN** compilation fails with an error that the rule cannot list itself

### Requirement: Group references in `after:` and `before:`
Rule-level `after:` and `before:` SHALL accept group names, local or module-qualified, alongside rule names. A group reference SHALL respect `pub` visibility. The referring rule SHALL have a handler for the group's phase; otherwise compilation SHALL fail. A rule SHALL NOT list a group it is a member of. A group with no member handlers in the linked program SHALL produce no edges and no error.

#### Scenario: Rule runs after a group
- **WHEN** rule `ReactToGround` declares `after: phys.solve` and handles `fixed_tick`
- **THEN** `ReactToGround.fixed_tick` executes after every member handler of `phys.solve`

#### Scenario: Group edges decide a two-way data conflict
- **WHEN** `MovePlayer` writes a field the member reads, the member writes a field `MovePlayer` reads, and `MovePlayer` declares `before: phys.solve`
- **THEN** `MovePlayer.fixed_tick` executes before the member handler regardless of linked declaration order

#### Scenario: Referring rule without a handler for the group's phase is rejected
- **WHEN** a rule handles only `tick` and declares `after: phys.solve` with `phys.solve` bound to `fixed_tick`
- **THEN** compilation fails with an error naming the rule, the group, and the phase

#### Scenario: Private group is not visible to other modules
- **WHEN** `std.physics.volume` declares `group solve:` without `pub` and a game rule declares `before: phys.solve`
- **THEN** compilation fails with a visibility error

#### Scenario: Member listing its own group is rejected
- **WHEN** a rule declares `group: solve` and `after: solve`
- **THEN** compilation fails with an error that a member cannot order against its own group

#### Scenario: Empty group is a no-op
- **WHEN** a rule declares `before: solve` and no linked rule is a member of `solve`
- **THEN** the program compiles and no edge is created

#### Scenario: Group ordering cycle is rejected
- **WHEN** a member of `solve` declares `after: A` and rule A declares `after: solve`
- **THEN** compilation fails with the handler-level cycle path

### Requirement: Cross-module group ordering
Group declarations, group membership, and group references SHALL be preserved in module artifacts. Group references SHALL be expanded into member-handler edges over the whole linked program, so ordering is the same whether the modules are compiled together or separately.

#### Scenario: Separately compiled modules keep group order
- **WHEN** `std.physics.volume` declares `pub group solve:` with member `MoveAndSlide`, is compiled to an artifact, and a separately compiled game module declares `before: phys.solve` on `MovePlayer`
- **THEN** the linked schedule orders `MovePlayer.fixed_tick` before `MoveAndSlide.fixed_tick`

#### Scenario: Headless behavior follows group order
- **WHEN** a game rule declared `before:` a group writes a value that the group member copies into another field in the same `fixed_tick`
- **THEN** after one fixed step, the copied field holds the value written in that same step
