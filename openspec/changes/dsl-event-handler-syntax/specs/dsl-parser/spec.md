## MODIFIED Requirements

### Requirement: Lifecycle event handler grammar
The parser SHALL accept `on` handlers using the new parameter-free syntax. The `( param_list )` is removed entirely. An optional `as IDENTIFIER` alias clause is added after the event name. The event name continues to accept both reserved lifecycle keywords and user-defined identifiers.

```ebnf
event_handler = "on" event_name [ "as" IDENTIFIER ] ":" NEWLINE INDENT
                { statement }
                DEDENT ;

event_name = "tick" | "fixed_tick" | "late_tick"
           | "spawn" | "destroy" | "load" | "unload"
           | "input" | IDENTIFIER ;
```

The handler no longer carries a parameter list node; instead, `EventHandlerNode` has an optional `alias: string` field.

#### Scenario: on tick handler parsed without parameters
- **WHEN** `on tick:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "tick"`, no params, and `alias = nil`

#### Scenario: on tick with alias parsed
- **WHEN** `on tick as t:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "tick"` and `alias = "t"`

#### Scenario: on fixed_tick handler parsed without parameters
- **WHEN** `on fixed_tick:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "fixed_tick"` and `alias = nil`

#### Scenario: on late_tick handler parsed
- **WHEN** `on late_tick:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "late_tick"` and `alias = nil`

#### Scenario: on spawn handler parsed
- **WHEN** `on spawn:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "spawn"` and `alias = nil`

#### Scenario: on destroy handler parsed
- **WHEN** `on destroy:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "destroy"` and `alias = nil`

#### Scenario: on load handler parsed
- **WHEN** `on load:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "load"` and `alias = nil`

#### Scenario: on unload handler parsed
- **WHEN** `on unload:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "unload"` and `alias = nil`

#### Scenario: on input() handler parsed (no parameters)
- **WHEN** `on input:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "input"` and `alias = nil`

#### Scenario: User event handler with alias parsed
- **WHEN** `on PlayerDamaged as dmg:` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "PlayerDamaged"` and `alias = "dmg"`

#### Scenario: Old parameter syntax produces parse error
- **WHEN** `on tick(dt: float):` appears in a system body
- **THEN** the parser reports an error: "unexpected '('; event handlers no longer take a parameter list; use 'on tick:' and access fields as 'tick.dt'"

### Requirement: Marker event declaration (body is optional)
The parser SHALL accept `event` declarations with no colon and no body. The event body (colon + indented block) is optional, consistent with the marker trait pattern:

```ebnf
event_decl = [ "pub" ] "event" IDENTIFIER
             [ ":" NEWLINE INDENT
               { field_decl }
               DEDENT ] ;
```

#### Scenario: Marker event (no colon) parsed
- **WHEN** `pub event spawn` appears at the top level with no colon and no body
- **THEN** the parser produces an `EventDecl` with `name = "spawn"` and empty `fields` list

#### Scenario: Event with fields still parsed
- **WHEN** `event PlayerDamaged:` followed by an indented body appears
- **THEN** the parser produces an `EventDecl` with the declared fields (existing behavior unchanged)
