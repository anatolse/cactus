# stdlib-standard-ui Specification

## Purpose

Define the shipped ECS-backed Standard UI surface, authored layout behavior, deterministic painter semantics, animation, and backend-rendered window presentation without adding UI-specific core-language constructs.

## Requirements

### Requirement: `std.ui` exposes the standard node and layout traits
The `std.ui` module SHALL expose `Node`, `Visual`, `PreferredSize`, `DesiredSize`, `Anchors`, and `ComputedLayout` traits. `Node` SHALL contain logical `visible`, `enabled`, `z_index`, and `clip_children` state. `Visual` SHALL contain presentation `scale` and `opacity`. `PreferredSize` SHALL contain an authored minimum size, `DesiredSize` SHALL contain frame-local measured size, and `ComputedLayout` SHALL contain window-space logical bounds, effective inherited state, effective opacity, ancestor clip bounds, and deterministic painter order.

`DesiredSize` and `ComputedLayout` SHALL be produced by ordinary Cactus `project` statements and SHALL obey the standard projected-trait lifetime and coalescing semantics.

#### Scenario: Standard layout traits are importable
- **WHEN** authored code imports `std.ui as ui`
- **THEN** it can apply `ui.Node`, `ui.Visual`, `ui.PreferredSize`, and `ui.Anchors` and can filter on projected `ui.DesiredSize` and `ui.ComputedLayout`

#### Scenario: Computed layout is frame-local
- **WHEN** the standard arrangement rule projects `ComputedLayout` during a frame
- **THEN** later pointer and render handlers observe that value during the frame and it is absent next frame unless projected again

### Requirement: non-UI structural parents provide the window root rectangle
A `Node` whose direct parent is absent, stale, or does not carry `Node` SHALL be a root of the UI forest and SHALL receive the complete current window rectangle as its parent-provided slot. A non-`Node` structural parent SHALL NOT itself be measured, arranged, rendered, or hit-tested by Standard UI.

#### Scenario: Non-Node entity owns a UI subtree
- **WHEN** an ordinary entity without `Node` has a child carrying `Node`
- **THEN** the child resolves its layout against the complete window rectangle while the ordinary parent remains outside the UI painter and pointer domains

#### Scenario: Parentless Node is window-rooted
- **WHEN** a live `Node` has no direct parent
- **THEN** it resolves against the complete window rectangle

### Requirement: `std.ui` exposes visual widget traits
The `std.ui` module SHALL expose `Panel`, `Text`, `Image`, and `Button` traits. `Panel` SHALL provide background, border color, and border width. `Text` SHALL provide text value, font size, color, and alignment. `Image` SHALL provide texture, tint, and `Stretch`, `Contain`, or `Cover` fitting. `Button` SHALL provide label, normal, hover, pressed, disabled, and text colors plus internal padding.

Standard UI text SHALL participate in measurement, effective visibility and opacity, clipping, and the unified painter order rather than using the independent low-level `ScreenLabel` overlay path.

#### Scenario: Standard text participates in layout
- **WHEN** a `Node` has `Text` and no larger authored minimum size
- **THEN** its measured desired size includes the text metrics for its value and font size

#### Scenario: Existing screen label remains available
- **WHEN** a program continues to use `std.render.text.ScreenLabel`
- **THEN** that low-level label remains valid and does not require conversion to `std.ui.Text`

### Requirement: `std.ui` exposes Stack, Grid, and Overlay containers
The `std.ui` module SHALL expose `Stack`, `Grid`, `GridItem`, and `Overlay`. Each container SHALL provide symmetric content padding; Stack SHALL provide axis, gap, and cross-axis alignment; Grid SHALL provide columns, cell size, two-axis gap, and cell/span placement; Overlay SHALL allocate its content rectangle to each direct child.

Layout order within Stack and automatic Grid placement SHALL use stable entity creation order and SHALL NOT be changed by `z_index`. If an entity carries more than one container trait, Standard UI SHALL choose `Stack`, then `Grid`, then `Overlay` in that deterministic precedence order.

#### Scenario: Vertical stack uses stable child order
- **WHEN** a vertical Stack has three direct Node children created in source order
- **THEN** their slots are allocated top-to-bottom in that creation order regardless of their `z_index` values

#### Scenario: Multiple container traits have deterministic behavior
- **WHEN** one Node carries both Stack and Grid
- **THEN** its direct children are laid out using Stack behavior

### Requirement: Standard UI measurement is authored in Cactus
The shipped `std.ui` module SHALL implement measurement through an ordinary selectionless Cactus rule that iterates a trait-filtered hierarchy-postorder snapshot and projects `DesiredSize`. The backend SHALL provide bounded hierarchy and intrinsic metric facts but SHALL NOT hardcode Stack, Grid, Overlay, anchor, or widget sizing policy.

Leaf measurement SHALL combine intrinsic widget size with `PreferredSize` as a component-wise minimum. Container measurement SHALL reduce already-measured direct-child sizes: Stack along its configured axis, Grid from its cells/spans, and Overlay by component-wise child maxima, including padding and gaps.

#### Scenario: Child is measured before parent
- **WHEN** a Stack contains a Text child
- **THEN** the postorder measurement rule projects the child's DesiredSize before reducing that value into the Stack's DesiredSize

#### Scenario: Preferred size is a minimum
- **WHEN** intrinsic text metrics are smaller than the Node's PreferredSize on either axis
- **THEN** DesiredSize on that axis is at least the corresponding PreferredSize value

#### Scenario: Container policy remains authored
- **WHEN** the shipped Stack gap or padding policy is inspected
- **THEN** that calculation is present in ordinary `std.ui` Cactus rule bodies rather than only in a backend implementation

### Requirement: Standard UI arrangement is authored in Cactus
The shipped `std.ui` module SHALL implement arrangement through an ordinary selectionless Cactus rule that iterates UI roots and a trait-filtered hierarchy-preorder snapshot and projects `ComputedLayout` to each Node. A parent SHALL be arranged before its direct children. Stack, Grid, and Overlay SHALL allocate child slots, after which explicit Anchors resolve within each slot.

When a child has no `Anchors`, it SHALL fill the slot allocated by its parent container. Stack cross-axis alignment MAY reduce and position that filled slot according to the Stack alignment. Explicit anchors SHALL override this implicit slot occupation.

#### Scenario: Arbitrarily nested UI arranges in one pass
- **WHEN** a UI forest contains a Stack, a child Grid, and a grandchild Button
- **THEN** preorder arrangement computes the Stack before the Grid and the Grid before the Button without a fixed hierarchy-depth limit

#### Scenario: Unanchored Grid child fills its cell
- **WHEN** a Grid child has no Anchors
- **THEN** its logical bounds occupy the assigned cell or span

### Requirement: anchors resolve fixed and stretched axes inside allocated slots
`Anchors` SHALL define normalized `min` and `max`, `pivot`, local `offset`, and minimum/maximum margins. For each axis where `min == max`, the child SHALL use DesiredSize on that axis and position it at the normalized anchor plus offset relative to pivot. For each axis where `min != max`, the child SHALL stretch between the normalized endpoints after applying margins; pivot SHALL NOT alter stretched logical bounds.

Invalid resolved negative sizes SHALL clamp to zero. `Visual.scale` SHALL use pivot for presentation but SHALL NOT alter logical layout or pointer hit bounds.

#### Scenario: Centered fixed-size dialog
- **WHEN** a Node uses equal anchors at 0.5, pivot 0.5, and DesiredSize 420 by 320
- **THEN** it is centered in its allocated slot with logical size 420 by 320

#### Scenario: Stretched content uses margins
- **WHEN** a Node anchors from 0 to 1 and supplies 20-pixel minimum and maximum margins
- **THEN** its logical bounds fill the slot minus those margins

#### Scenario: Visual bump retains logical hit bounds
- **WHEN** a button's Visual scale animates from 0.8 to 1.0
- **THEN** its ComputedLayout size and pointer hit rectangle remain unchanged

### Requirement: z-index creates sibling-local stacking contexts
`Node.z_index` SHALL order only direct siblings. Direct siblings SHALL be painted by ascending `z_index` with stable creation ordinal as the final tie-breaker, and each child's complete subtree SHALL remain atomic relative to the next sibling subtree. The resulting recursive traversal SHALL assign a monotonic `ComputedLayout.draw_order` used consistently by rendering and window-space pointer picking.

#### Scenario: Descendant cannot escape ancestor stacking context
- **WHEN** sibling A has `z_index = 0`, sibling B has `z_index = 1`, and A has a child with `z_index = 1000`
- **THEN** A's complete subtree is painted before B and the high-z descendant does not paint or receive window hits above B

#### Scenario: Creation ordinal breaks sibling tie
- **WHEN** two overlapping siblings have equal z-index
- **THEN** their stable creation ordinals deterministically choose paint and reverse-hit order

### Requirement: inherited state and clipping propagate through the UI tree
Arrangement SHALL compute effective visibility and enabled state by logical conjunction with ancestors, effective opacity by multiplication with ancestor opacity, and effective clip bounds by intersecting ancestor clip rectangles when `clip_children` is enabled. Invisible Nodes SHALL not render or participate in pointer hits; disabled Nodes SHALL not activate pointer behavior; clipped pixels SHALL not render or hit outside the effective clip.

#### Scenario: Hidden parent hides descendants
- **WHEN** a parent Node has `visible = false`
- **THEN** no descendant visual is rendered or selected by window-space pointer picking

#### Scenario: Nested clips intersect
- **WHEN** two clipping ancestors have partially overlapping content rectangles
- **THEN** a descendant is rendered and hit-tested only inside their rectangle intersection

### Requirement: Standard UI renders as one ordered window-space painter pass
Standard UI SHALL render Panel, Image, Text, and Button visuals in the order represented by `ComputedLayout.draw_order`, with nested clipping and effective opacity. The pass SHALL render after viewport/world content and SHALL use the same logical bounds and ordering consumed by pointer picking.

Within one entity, the standard primitive order SHALL be background, image, button fill, text or button label, then border. `ImageFit.Cover` SHALL crop to the destination bounds, while `Contain` SHALL preserve the complete image inside them.

#### Scenario: Visual topmost matches pointer topmost
- **WHEN** two enabled sibling buttons overlap
- **THEN** the button painted last is also the first window-space pointer candidate at their overlap

#### Scenario: UI overlays world rendering
- **WHEN** a window-space Panel overlaps a rendered world object
- **THEN** the Panel is drawn after the world content

### Requirement: Standard UI provides frame and visual animation traits
The `std.ui` module SHALL expose `FrameAnimation` for image frame progression and `BumpAnimation` plus a targeted `StartBump` event for reusable Visual scale animation. The shipped animation policy SHALL be authored as ordinary Cactus rules. Frame animation SHALL use a documented horizontal-strip frame convention in the initial capability, clamp invalid frame counts to one, and leave non-playing animations unchanged.

#### Scenario: Targeted bump affects one entity
- **WHEN** `StartBump` is emitted to one entity carrying Visual and BumpAnimation
- **THEN** only that targeted entity restarts its bump state and scale animation

#### Scenario: Frame animation advances deterministically
- **WHEN** a playing FrameAnimation has positive fps and sufficient elapsed tick time for one frame
- **THEN** its frame advances modulo the effective frame count

### Requirement: Standard UI runs layout before input and before rendering
The standard graph SHALL measure and arrange UI in the input phase before generic pointer routing, advance animation during tick, measure and arrange again during late_tick, and render during the render phase. The input layout SHALL provide current logical hit bounds; the late layout SHALL reflect gameplay and animation changes visible in that frame's render.

#### Scenario: Input consumes current layout
- **WHEN** a layout-affecting value was committed before the input phase
- **THEN** pointer routing that frame reads the ComputedLayout projected by input measurement and arrangement

#### Scenario: Tick animation is visible the same frame
- **WHEN** a tick animation changes Visual presentation
- **THEN** the later layout/render path uses the updated presentation during that frame's render
