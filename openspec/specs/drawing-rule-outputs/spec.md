# drawing-rule-outputs Specification

## Purpose

Define what a drawing rule's fragment stage writes and how it reaches the picture: a cover color and an added light composed in one premultiplied blend, and texture sampling from any texture a stage can read.

## Requirements

### Requirement: Cover and light outputs
The fragment alias of a drawing rule SHALL expose two outputs, `frag_color: color` and `light: color`. An output the handler does not assign SHALL be transparent. A path that assigns neither SHALL be an error. No rule clause or enum SHALL select a blend mode.

#### Scenario: Cover only
- **WHEN** a fragment handler assigns `frag_color = #FF0000FF` and not `light`
- **THEN** the pixel is red, whatever was under it

#### Scenario: Neither output on a path
- **WHEN** a fragment handler assigns `frag_color` in an `if` branch and nothing in the `else` branch
- **THEN** the compiler reports an error saying a path assigns no output

### Requirement: How the outputs reach the pixel
For each of red, green and blue, the drawn pixel SHALL be `under × (1 − c.a) + s`, where `under` is the value already drawn, `c` is `frag_color`, `l` is `light` and `s` is `c × c.a + l × l.a` clamped to 0..1. The alphas `c.a` and `l.a` SHALL be clamped to 0..1 first. The color channels SHALL stay unclamped inside the stage; `s` is clamped before it meets `under`, so a negative channel subtracts nothing, and the result is clamped again when it is written.

#### Scenario: Light only
- **WHEN** a fragment handler assigns `light = #404040FF` over a pixel that was `#202020`
- **THEN** the pixel is `#606060`

#### Scenario: Two lights add
- **WHEN** two quads with `light = #404040FF` overlap over black
- **THEN** the overlap is `#808080`

#### Scenario: Cover and light in one pixel
- **WHEN** a handler assigns `frag_color = #00000080` and `light = #400000FF` over white
- **THEN** the pixel is half-dark grey with red added: about `#C08080`

#### Scenario: Transparent cover with color
- **WHEN** a handler assigns `frag_color = #FF000000` and not `light`
- **THEN** the pixel is unchanged

#### Scenario: Bright light
- **WHEN** a handler assigns `light = #FF8000FF * 3.0` over black
- **THEN** the pixel is `#FFFF00`: alpha counts as 1 and each channel clamps on the write

#### Scenario: Negative light
- **WHEN** a handler assigns `light = color(-0.5, -0.5, -0.5, 1.0)` over a pixel that was `#202020`
- **THEN** the pixel stays `#202020`: light only adds

### Requirement: Texture sampling in the fragment stage
`std.render.textures` SHALL provide `pub extern func sample(texture: texture_id, uv: vec2) color`, callable only as `stage-function-translation` allows for fragment-only functions. It SHALL return the texture's straight (not premultiplied) color at `uv`, with (0, 0) the top-left and (1, 1) the bottom-right, and with `uv` clamped to that range.

#### Scenario: Outside the image
- **WHEN** `sample` is called with `uv = (1.5, 0.5)`
- **THEN** it returns the color at the right edge

#### Scenario: Wrong place
- **WHEN** a vertex handler or an ordinary rule calls `textures.sample`
- **THEN** the compiler reports an error saying `sample` belongs to a fragment stage

### Requirement: Where a sampled texture comes from
The `texture` argument of `sample` MAY be any value a stage can read: a field of the rule's row, a field reached through a link, a field of a named entity, or an asset constant. Rows of one rule that use different textures SHALL each be drawn with their own. A handle that does not resolve SHALL sample as `#00000000` and SHALL be noted as a missing asset.

#### Scenario: Row texture
- **WHEN** a rule over `each: Sprite as sprite` assigns `f.frag_color = textures.sample(sprite.texture, f.uv)`
- **THEN** each quad shows its own sprite's image

#### Scenario: Asset constant
- **WHEN** a fragment handler calls `textures.sample(Soft, f.uv)` with `Soft` an `asset ... : texture`
- **THEN** the quad shows that image

#### Scenario: Missing texture
- **WHEN** a row's texture handle does not resolve
- **THEN** the quad draws nothing and the missing asset is noted
