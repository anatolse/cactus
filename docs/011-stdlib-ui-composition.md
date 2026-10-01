# Proposal 011: Declarative UI Composition in the Standard Library

Status: draft  
Kind: stdlib and backend capability  
Core language change: none required

## Summary

Build UI from ordinary templates, traits, hierarchical `children`, targeted
events, and backend-owned traversal. Do not add core declarations such as
`view`, `panel`, `button`, `stack`, or `grid`.

The authored hierarchy expresses semantic widgets. Technical rendering and
layout implementation nodes are hidden inside stdlib/backend behavior.

## Goals

- readable panel, label, image, button, stack, and grid composition;
- deterministic parent-first layout;
- only the topmost eligible widget receives pointer input;
- animation and visual state as data, not imperative rendering calls;
- frame-local layout and interaction facts outside durable gameplay state;
- no author-written pair passes for ordinary stack/grid layout.

## Non-goals

- no general-purpose DOM;
- no arbitrary retained-mode scripting language;
- no core UI keywords;
- no `limit: 1` rule feature;
- no requirement that gameplay rules inspect presentation-only projected facts;
- no manual renderer calls in ordinary UI authoring.

## Initial stdlib data model

Suggested durable configuration traits, declared inside `module std.ui`:

```cactus
pub enum LengthKind:
    Pixels
    Percent
    Content
    Fill

pub struct Length:
    kind: LengthKind
    value: float

pub func px(value: float) Length:
    return Length(LengthKind.Pixels, value)

pub func percent(value: float) Length:
    return Length(LengthKind.Percent, value)

pub func content() Length:
    return Length(LengthKind.Content, 0.0)

pub func fill(weight: float) Length:
    return Length(LengthKind.Fill, weight)

pub enum Direction:
    Horizontal
    Vertical

pub enum Align:
    Start
    Center
    End
    Stretch

pub trait Element:
    var visible: bool = true
    var enabled: bool = true

pub trait Size:
    var width: Length = content()
    var height: Length = content()

pub trait Padding:
    var left: float = 0.0
    var top: float = 0.0
    var right: float = 0.0
    var bottom: float = 0.0

pub trait Stack:
    var direction: Direction = Direction.Vertical
    var gap: float = 0.0
    var align: Align = Align.Start

pub trait Grid:
    var columns: int = 1
    var row_gap: float = 0.0
    var column_gap: float = 0.0

pub trait PanelStyle:
    var color: color = #FFFFFFFF
    var corner_radius: float = 0.0

pub trait Text:
    var text: string = ""
    var font_size: int = 16
    var color: color = #FFFFFFFF

pub trait Image:
    let texture: texture_id
    var tint: color = #FFFFFFFF

pub trait Button:
    var focusable: bool = true
```

Suggested frame-local projected traits:

```cactus
pub trait LayoutRect:
    var position: vec2
    var size: vec2
    var depth: int

pub trait Hovered
pub trait Pressed
pub trait Focused
```

`LayoutRect` and interaction facts are recomputed and cleared at the frame
boundary. They are presentation products, not persistent gameplay state.

## Widget templates

Common widgets are stdlib templates that combine traits and internal behavior:

```cactus
pub template Panel:
    Element
    Size
    Padding
    PanelStyle

pub template Label(text: string, font_size: int = 16):
    Element
    Text:
        text = text
        font_size = font_size

pub template ImageWidget(texture: texture_id):
    Element
    Image:
        texture = texture

pub template ButtonWidget(text: string):
    Element
    Size
    Button
    PanelStyle
    Text:
        text = text
```

The public `ui.ButtonWidget` template need not expose background and label as
authored child entities. The backend or stdlib renderer can derive those
technical primitives from the semantic widget traits.

## Authoring example

```cactus
entity ShopPanel from ui.Panel:
    ui.Size:
        width = ui.px(360.0)
        height = ui.content()
    ui.Padding:
        left = 16.0
        top = 16.0
        right = 16.0
        bottom = 16.0
    ui.Stack:
        direction = ui.Direction.Vertical
        gap = 8.0

    children:
        Title from ui.Label(text = "Cactus Shop", font_size = 24)
        ProductImage from ui.ImageWidget(texture = CactusTexture)
        BuyButton from ui.ButtonWidget(text = "Buy")
        CloseButton from ui.ButtonWidget(text = "Close")
```

Proposal 004 shortens child declarations, proposal 006 supplies named widget
parameters, and proposal 010 supplies `each:` if desired. None is required for
the underlying UI semantics.

## Layout traversal

The backend owns hierarchy traversal because arbitrary nesting cannot be
expressed correctly as independent one-level pair passes.

Required order:

1. Identify visible UI roots in stable root order.
2. Traverse parent-first using generated `Parent` relations and stable child
   creation order.
3. Measure intrinsic content.
4. Resolve parent constraints, padding, stack/grid placement, and final rects.
5. Project one `LayoutRect` per visible widget.
6. Render in stable depth and traversal order.

Layout is one graph-driven presentation capability even if the backend uses
multiple internal passes. Structural commits remain activation boundaries.

## Pointer routing

Input routing is not an ordinary broadcast event.

1. Collect visible, enabled hit candidates from current `LayoutRect` facts.
2. Traverse in reverse paint order.
3. Select the first eligible candidate.
4. Project `Hovered`/`Pressed` to that entity.
5. Emit a targeted widget event.

```cactus
pub event Activated

rule BuyProduct:
    filter:
        BuyAction

    on ui.Activated:
        emit PurchaseRequested
```

Only the selected topmost entity receives the targeted `ui.Activated`
occurrence. There is no author-visible `limit: 1`, sorting trick, or broadcast
followed by self-filtering.

## Keyboard/gamepad focus

Focus order uses the same stable traversal order over visible, enabled,
focusable widgets. At most one widget owns the frame-local `Focused` fact.
Directional navigation may later use layout geometry, but the initial proposal
can support next/previous focus only.

## Animation

Animation is expressed as configuration and derived presentation state:

```cactus
ui.Transition:
    property = ui.Property.Scale
    duration = 0.18
    easing = ui.Easing.BackOut
```

Panel bump, button hover, and image frame animation belong to stdlib/backend
presentation processing. Handlers should react to semantic activation events,
not manually advance visual interpolation unless gameplay requires it.

## Editor simplification

This capability should replace editor-specific plumbing such as fixed label
slot pools, manual palette rectangle hit testing, and backend helpers for
button Y positions.

Before migrating the editor, separately fix:

- mutation of handler-local `var` across `if`/bounded `for` scopes;
- explicit numeric conversion such as `float(index)`;
- enum-typed `EditorState.mode` instead of magic integers;
- dead projected gizmo traits and unused events/rules.

The editor may legitimately use a `pairs` join between singleton `EditorState`
and `EditorSelected` for gizmo rendering. Ordinary UI layout should not require
authors to write pair rules.

## External contracts and graph model

The stdlib/backend should expose explicit graph nodes or external handlers for:

- layout production;
- pointer/focus routing;
- rendering;
- optional animation projection.

Their reads, projected outputs, event emissions, and graphics/input effects
must be explicit contracts. Scheduling is graph-driven, not based on magic rule
names.

## Implementation stages

1. `Element`, `Size`, `Padding`, `Stack`, `LayoutRect`, panel and text rendering.
2. Pointer hit testing and targeted `Activated`.
3. Button template and state visuals.
4. Grid layout and intrinsic image sizing.
5. Keyboard/gamepad focus.
6. Transitions and editor palette migration.

## Acceptance criteria

- A panel with text, animated image, and two buttons requires no manual drawing
  or rectangle hit testing.
- Nested stacks and grids receive deterministic layout across backends.
- Only the topmost eligible button receives pointer activation.
- Layout and interaction facts are cleared at the frame boundary.
- Gameplay state does not need to store pixel rectangles or hover flags.
- UI hierarchy uses ordinary `children` and generated `Parent` relations.
- No new core UI declaration keyword is required.

## CIR impact

No new general language construct is required. CIR needs to preserve existing
hierarchy data, projected outputs, targeted event flow, and explicit external
handler contracts. Backend-owned hierarchy traversal may be represented as a
dedicated external handler implementation, not as runtime-expanded graph nodes
per widget.
