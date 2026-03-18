## ADDED Requirements

### Requirement: `asset_decl` grammar production
The parser SHALL parse `asset` declarations as top-level declarations. The `asset` keyword SHALL be added to the keyword list and the `declaration` production updated to include `asset_decl`.

```ebnf
asset_decl = [ "pub" ] "asset" IDENTIFIER ":" asset_type "=" STRING_LITERAL NEWLINE ;
asset_type = "mesh" | "texture" | "sound" | "music" | "font" | "material" ;

declaration = module_decl | use_decl | const_block | struct_decl
            | enum_decl | trait_decl | unit_decl | template_decl | system_decl
            | event_decl | func_decl | asset_decl | input_decl ;
```

#### Scenario: Asset declaration at top level parsed
- **WHEN** `asset ShotSound: sound = "audio/shot.wav"` appears at the top level
- **THEN** the parser produces an `AssetDecl` node with `name = "ShotSound"`, `asset_type = sound`, `path = "audio/shot.wav"`

#### Scenario: Pub asset declaration parsed
- **WHEN** `pub asset SharedMesh: mesh = "models/shared.glb"` appears at the top level
- **THEN** the parser produces an `AssetDecl` with `is_pub = true`

#### Scenario: Asset declaration inside system body rejected
- **WHEN** `asset Foo: texture = "foo.png"` appears inside a system event handler
- **THEN** the parser reports an error: `asset` declarations are only valid at top level

### Requirement: `input_decl` grammar production
The parser SHALL parse `input` declarations as top-level declarations. The `input` keyword SHALL be added to the keyword list and the `declaration` production updated to include `input_decl`.

```ebnf
input_decl = [ "pub" ] "input" IDENTIFIER ":" ( "button" | "axis" ) NEWLINE INDENT
             { input_prop }
             DEDENT ;
input_prop = IDENTIFIER "=" expression NEWLINE ;
```

#### Scenario: Button input declaration parsed
- **WHEN** the following source appears at the top level:
  ```
  input Jump: button
      key     = Key.Space
      gamepad = GamepadButton.South
  ```
- **THEN** the parser produces an `InputDecl` with `name = "Jump"`, `kind = button`, and two `InputProp` children

#### Scenario: Axis input declaration with invert parsed
- **WHEN** an axis input includes `invert = true`
- **THEN** the parser produces an `InputProp` with `key = "invert"` and a `BoolLiteral(true)` value expression

#### Scenario: Input declaration missing kind rejected
- **WHEN** `input Jump:` appears with no `button` or `axis` keyword
- **THEN** the parser reports an error: expected `button` or `axis` after `:`

## MODIFIED Requirements

### Requirement: `on spawn()`, `on destroy()`, `on load()`, `on unload()` lifecycle handler grammar
The parser SHALL accept `on spawn()`, `on destroy()`, `on load()`, `on unload()`, `on input()`, `on fixed_tick(dt: float)`, and `on late_tick(dt: float)` as lifecycle event handler forms. The `event_name` production is extended:

```ebnf
event_handler = "on" event_name "(" [ param_list ] ")" ":" NEWLINE INDENT
                { statement }
                DEDENT ;

event_name = "tick" | "fixed_tick" | "late_tick"
           | "spawn" | "destroy" | "load" | "unload"
           | "input" | IDENTIFIER ;
```

The keywords `fixed_tick` and `late_tick` are added to the keyword list. (`input` is already added via the `input_decl` addition.)

#### Scenario: on spawn handler parsed
- **WHEN** `on spawn():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "spawn"` and empty param list

#### Scenario: on destroy handler parsed
- **WHEN** `on destroy():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "destroy"` and empty param list

#### Scenario: on load handler parsed
- **WHEN** `on load():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "load"` and empty param list

#### Scenario: on unload handler parsed
- **WHEN** `on unload():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "unload"` and empty param list

#### Scenario: on input() handler parsed (no parameters)
- **WHEN** `on input():` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "input"` and empty param list

#### Scenario: on fixed_tick handler parsed
- **WHEN** `on fixed_tick(dt: float):` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "fixed_tick"` and one typed parameter `dt: float`

#### Scenario: on late_tick handler parsed
- **WHEN** `on late_tick(dt: float):` appears in a system body
- **THEN** the parser produces an `EventHandler` with `event_name = "late_tick"` and one typed parameter `dt: float`

### Requirement: Top-level declaration parsing
The parser SHALL parse a sequence of top-level declarations from the token stream, producing a ProgramNode as the AST root. Supported declarations: `module`, `use`, `const`, `struct`, `enum`, `trait`, `unit`, `system`, `view`, `event`, `func`, `interface`, `template`, `asset`, `input`.

#### Scenario: Module and trait declarations
- **WHEN** the source contains a `module` declaration followed by a `trait` declaration
- **THEN** the parser produces a ProgramNode containing a ModuleNode and a TraitNode

#### Scenario: Asset declaration in program
- **WHEN** the source contains `asset PlayerMesh: mesh = "player.glb"` at the top level
- **THEN** the parser produces a ProgramNode containing an `AssetDecl` node

#### Scenario: Input declaration in program
- **WHEN** the source contains an `input Jump: button` declaration at the top level
- **THEN** the parser produces a ProgramNode containing an `InputDecl` node

#### Scenario: Unknown top-level keyword
- **WHEN** the source contains an unrecognized keyword at the top level
- **THEN** the parser reports an error with the source location and expected declaration types
