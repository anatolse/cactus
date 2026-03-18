## ADDED Requirements

### Requirement: Asset declaration syntax
The parser SHALL accept `asset` as a top-level declaration form. An `asset` declaration binds a compile-time identifier to a typed external resource file path.

```ebnf
asset_decl  = [ "pub" ] "asset" IDENTIFIER ":" asset_type "=" STRING_LITERAL NEWLINE ;
asset_type  = "mesh" | "texture" | "sound" | "music" | "font" | "material" ;
```

The keyword `asset` SHALL be added to the keyword list and to the `declaration` production.

#### Scenario: Basic asset declaration parsed
- **WHEN** `asset PlayerMesh: mesh = "models/player.glb"` appears at the top level
- **THEN** the parser produces an `AssetDecl` node with `name = "PlayerMesh"`, `asset_type = mesh`, and `path = "models/player.glb"`

#### Scenario: Pub asset declaration parsed
- **WHEN** `pub asset HudFont: font = "fonts/hud.ttf"` appears at the top level
- **THEN** the parser produces an `AssetDecl` node with `is_pub = true`, `name = "HudFont"`, `asset_type = font`

#### Scenario: All asset types accepted
- **WHEN** asset declarations use each of `mesh`, `texture`, `sound`, `music`, `font`, `material` as the type
- **THEN** the parser accepts each and produces an `AssetDecl` with the corresponding asset type

#### Scenario: Invalid asset type rejected
- **WHEN** `asset Foo: video = "foo.mp4"` appears at the top level
- **THEN** the parser reports an error listing the valid asset types

### Requirement: Asset identifiers resolve to opaque typed ID values
The semantic analyzer SHALL resolve asset declaration names to their corresponding built-in opaque ID type at the point of use. Each asset type maps to exactly one opaque ID type:

| Declaration type | Resolved type |
|-----------------|---------------|
| `mesh`          | `mesh_id`     |
| `texture`       | `texture_id`  |
| `sound`         | `sound_id`    |
| `music`         | `music_id`    |
| `font`          | `font_id`     |
| `material`      | `material_id` |

#### Scenario: Asset name used in trait field config
- **WHEN** a trait field `let mesh: mesh_id` is configured as `mesh = PlayerMesh` in a `unit` config block, and `PlayerMesh` is declared as `asset PlayerMesh: mesh = "..."`
- **THEN** the semantic analyzer resolves `PlayerMesh` to type `mesh_id` and accepts the assignment

#### Scenario: Type mismatch between asset kinds rejected
- **WHEN** `asset ShotSound: sound = "audio/shot.wav"` is used where a `mesh_id` is expected
- **THEN** the semantic analyzer reports a type mismatch error

#### Scenario: Undeclared asset identifier rejected
- **WHEN** a config block references `UnknownAsset` which has no `asset` declaration in scope
- **THEN** the semantic analyzer reports an undeclared identifier error

### Requirement: Asset path string literals are exempt from the const-string rule
String literals SHALL be permitted as the path value in `asset` declarations. This is the sole exception to the rule that string literals are not permitted outside `const` blocks. String literals in any other position outside `const` blocks remain a semantic error.

#### Scenario: String literal in asset declaration accepted
- **WHEN** `asset Theme: music = "audio/theme.ogg"` appears in a source file that has no `const` block
- **THEN** the parser and semantic analyzer accept the string literal without error

#### Scenario: String literal in system handler still rejected
- **WHEN** a system handler contains `let path = "assets/foo.png"` (a string literal in logic)
- **THEN** the semantic analyzer reports an error: string literals are not permitted outside `const` blocks or `asset` declarations

### Requirement: Asset declarations participate in module visibility
An `asset` declaration without `pub` SHALL be module-private. A `pub asset` declaration SHALL be importable by other modules via `use`.

#### Scenario: Non-pub asset not visible to importer
- **WHEN** module A declares `asset Foo: texture = "foo.png"` (no `pub`) and module B does `use A` and references `Foo`
- **THEN** the semantic analyzer reports an error: `Foo` is not exported from module A

#### Scenario: Pub asset visible to importer
- **WHEN** module A declares `pub asset SharedTex: texture = "shared.png"` and module B imports A
- **THEN** `SharedTex` is available in module B with type `texture_id`

### Requirement: Assets are declared at module scope only
Asset declarations SHALL be permitted only at the top level of a module. They SHALL NOT be permitted inside trait bodies, system bodies, func bodies, or any other nested scope.

#### Scenario: Asset at top level accepted
- **WHEN** `asset Snd: sound = "hit.wav"` appears as a top-level declaration
- **THEN** the parser accepts it

#### Scenario: Asset inside system body rejected
- **WHEN** `asset Snd: sound = "hit.wav"` appears inside a `system` block
- **THEN** the parser reports an error: asset declarations are only allowed at module top level
