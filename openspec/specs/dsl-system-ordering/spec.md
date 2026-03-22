## Requirements

### Requirement: `after:` clause on system declarations
A `system` declaration MAY include an optional `after:` clause that lists one or more system names, one per indented line (identical block structure to `filter:` and `exclude:`). The `after:` clause declares that this system must execute after all listed systems within the same execution phase. The clause appears after `filter:` and `exclude:` blocks and before the first event handler.

```ebnf
system_decl     = "system" IDENTIFIER ":" NEWLINE INDENT
                  [ filter_clause ]
                  [ exclude_clause ]
                  [ after_clause ]
                  { event_handler }
                  DEDENT ;
after_clause    = "after" ":" NEWLINE INDENT
                  { IDENTIFIER NEWLINE }
                  DEDENT ;
```

Example:
```cactus
system SceneRenderSystem:
    filter:
        Renderer as r
    on tick(dt: float):
        # draw world objects

system UIRenderSystem:
    filter:
        UIRenderer as ui
    after:
        SceneRenderSystem
    on tick(dt: float):
        # draw HUD on top of scene

system DebugOverlaySystem:
    after:
        SceneRenderSystem
        UIRenderSystem
    on tick(dt: float):
        # draw debug info on top of everything
```

#### Scenario: System with no `after:` clause is valid
- **WHEN** a `system` declaration contains no `after:` block
- **THEN** the parser accepts it and `SystemNode.after_systems` is empty

#### Scenario: System with single `after:` entry is valid
- **WHEN** a system has an `after:` block with one indented system name
- **THEN** `SystemNode.after_systems` contains exactly that one name

#### Scenario: System with multiple `after:` entries is valid
- **WHEN** a system has an `after:` block listing `SystemA`, `SystemB`, `SystemC` on separate lines
- **THEN** `SystemNode.after_systems` contains `["SystemA", "SystemB", "SystemC"]`

#### Scenario: `after:` appears after `filter:` and `exclude:` and before handlers
- **WHEN** a system body has `filter:`, then `exclude:`, then `after:`, then `on tick():`
- **THEN** the parser accepts the ordering and populates all clauses correctly

### Requirement: `after:` system name resolution
The semantic analyzer SHALL verify that every identifier listed in an `after:` clause resolves to a declared `system` in the current compiled program (all linked modules). Referencing a non-existent system name SHALL produce a compile error.

#### Scenario: Valid `after:` reference accepted
- **WHEN** `after: MovementSystem` is declared and `system MovementSystem:` exists in the same or an imported module
- **THEN** the semantic analyzer accepts the reference and adds the ordering edge to the dependency graph

#### Scenario: Unknown system name in `after:` rejected
- **WHEN** `after: NonExistentSystem` is declared and no system with that name exists
- **THEN** the semantic analyzer reports an error: "unknown system 'NonExistentSystem' in after clause"

#### Scenario: `after:` cannot reference a non-system declaration
- **WHEN** `after: Position` is declared and `Position` is a trait, not a system
- **THEN** the semantic analyzer reports an error: "'Position' is not a system"

### Requirement: `after:` ordering cycle detection
The semantic analyzer SHALL detect cycles in the `after:` ordering graph and report them as compile errors. A cycle occurs when system A is constrained to run after system B and system B is transitively constrained to run after system A.

#### Scenario: Direct cycle detected
- **WHEN** `system A:` declares `after: B` and `system B:` declares `after: A`
- **THEN** the semantic analyzer reports an error: "cycle in system ordering: A → B → A"

#### Scenario: Indirect (transitive) cycle detected
- **WHEN** `system A: after: B`, `system B: after: C`, `system C: after: A`
- **THEN** the semantic analyzer reports an error that identifies the cycle path

#### Scenario: Linear chain with no cycle is valid
- **WHEN** `system C: after: B` and `system B: after: A` with no back-edges
- **THEN** the semantic analyzer accepts the declarations and the execution order is A → B → C

### Requirement: `after:` edges stored in DecoratedProgram dependency graph
The semantic analyzer SHALL store validated `after:` ordering constraints in the `SystemInfo` structure inside the `DecoratedProgram`. Each system's `SystemInfo` SHALL include a list of system names that it must follow.

#### Scenario: Ordering edges visible in DecoratedProgram
- **WHEN** `system UIRenderSystem: after: SceneRenderSystem` is compiled
- **THEN** the `DecoratedProgram` contains `UIRenderSystem.after_systems = ["SceneRenderSystem"]`
