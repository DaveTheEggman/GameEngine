# UI layout and style model v2 - uniform layout style, a real cascade, transitions

> STATUS: P0 BUILT 2026-09-09 (LayoutStyle on View; LayoutParams classes, CreateDefaultLayoutParams,
> RegisterLayoutParam deleted; four lanes green). P1-P4 PROPOSED.
> P0 as built (deviations from the sketch below, all deliberate):
> - Field names follow the house PascalCase: `Width/Height/Margin/FlexGrow/FlexShrink/AlignSelf/
>   Gravity/Dock/Left/Top/GridRow/GridColumn/GridRowSpan/GridColumnSpan`. `FlexShrink` keeps the
>   old default 0 (FlexLayout does not read it yet); min/max/flexBasis/position/insets/zIndex
>   arrive with P1/P2 when their consumers land.
> - `View::Layout()` (const ref) + `View::SetLayout(style)` (marks layout damage only when the
>   value changed); `AddView(child)` KEEPS the child's style, `AddView(child, style)` sets it.
>   Same pair on `InsertView`, `Expander::SetContent/SetActions/SetHeaderActions`, `Docking
>   SetContent`. No mutable accessor: read, copy, change, `SetLayout`.
> - Grid auto-flow resolves cells per pass (`GridLayout::ResolvePlacements`) and never writes
>   the placement back; a child's `GridRow/GridColumn` stay -1 so reordering re-flows.
> - `Dock` and `Align` enums moved to `:layout_style` (the style needs them before the layouts).
> - Markup: `MarkupRegistry::LayoutAttributeNames()` + `ApplyLayoutAttribute` are the one
>   vocabulary (loader, completion, tests); every element accepts every layout attribute
>   whatever its parent, so `width/height/margin` on a ROOT element now take effect (they
>   were silently dropped before - screen overlays and badges sized by markup changed).
> - Unknown attributes stay cook WARNINGS (the tree still builds) rather than the hard load
>   error in section 3: the editor's live preview and the pipeline's warning contract depend
>   on partial loads while typing. The old `grow=`/`shrink=`/`dock=`/`gravity=` spellings are
>   unknown attributes (no aliases). Promoting unknown attributes to a load error is P1's call.
> - Hit-test visibility stays self-only (ToastHost/tool-float layers rely on children staying
>   hittable); the passive-badge test now probes outside the badge.
> Existing-implementation check (per the CONVENTIONS spec rule), everything cited
> was read on 2026-09-09:
> - Layout: `UI/Layout/LayoutParams.cppm` (base: Width/Height SizeSpec + Margin) and
>   FIVE per-layout subclasses (Absolute/Dock/Flex/Frame/Grid LayoutParams); every
>   View carries `RefPtr<LayoutParams>` minted by `View::CreateDefaultLayoutParams`
>   (one heap object per view, an RTTI cast per child per pass). `FlexLayout` has
>   no wrap. `SizeSpec` = Fixed/Match/Wrap. `Unit` = Dp/Px only.
> - Markup: `MarkupRegistry::RegisterLayoutParam(parentTag, attr, setter)` binds
>   child attributes to the PARENT's param type ("Flex","grow"); `MarkupLoader`
>   mints the param object per child from the parent tag.
> - Styling: `.sss` sheets (SSSParser vocabulary: background/border/text/font/
>   radius/padding/margin/opacity/width/height...), `StyleSelector` with type/
>   class/id + pseudo-class/pseudo-element and a `Specificity()`; `StyleSheet`
>   resolves each property by BEST specificity (ties: last wins) - a per-property
>   pick, not an ordered cascade; no inheritance, no variables (Palette only),
>   no calc/percent. Themes are moving to `.sss` (ui-theme-migration.md).
> - Animation: `UI/Animation` Storyboard/ViewAnimator/Easing - the base for
>   transitions; nothing today animates a STATE change.
> - Text: partial ellipsis (4 files), no shaping/RTL. Toolkit (20k lines, docking,
>   code editor, node graph, property grid) is NOT touched by this spec.
> Direction ruling (user 2026-09-09): EVOLVE our UI - not port-as-is to Beef, not
> greenfield. Do this in Raptor first; the Beef UI port follows it.

## Why

Measured against Godot's scene/gui, Lunarsong's XML+CSS UI, AUI and RmlUi, our UI
is missing the things that make a toolkit feel great rather than good: motion on
state changes, a complete box model on every view, a cascade that behaves like
CSS (specificity order, inheritance, variables, units), flex wrap + gap, and text
ellipsis. The per-child LayoutParams shape also blocks two of those: layout
attributes cannot be styled from a sheet or written in markup without knowing
the parent's type.

## Rulings

1. **Layouts are CORE.** Absolute, Dock, Flex, Flow, Frame, Grid ship with the
   engine. There are no per-layout parameter TYPES. The extension point for an
   exotic layout is a custom container (a ViewGroup that positions its children
   from the uniform properties) plus CUSTOM PROPERTIES (`--name`, below) for the
   rare per-child value the uniform set lacks.
2. **One uniform `LayoutStyle` value ON EVERY VIEW** replaces `RefPtr<LayoutParams>`
   and its five subclasses. Plain data, no allocation, contiguous, read by every
   layout for the subset it honours (what Yoga, RmlUi, AUI and Godot's size
   flags + anchors all do). eepp keeps its layout state on the widget too - the
   "dictionary on the layout" idea is a misremembering and is REJECTED (a hash
   lookup per child per pass, lifetime coupling on removal).
3. **CSS-proximate names everywhere.** Sheet properties, markup attributes and
   the C++ setters use the same names (`flex-grow`, `align-self`, `gap`,
   `grid-column`, `box-shadow`, `transition`). Proximity to real CSS is what
   RmlUi gets right; we take the vocabulary, not the web's layout engine.
4. **One layout, no legacy** (the serializer rule applies to sheets and markup):
   the old parent-typed attributes and the `LayoutParams` classes are DELETED,
   not aliased. Every `.sss` and `.sml` in the tree is rewritten in the same
   commit; the markup reader refuses an unknown attribute loudly.

## Design

### 1. LayoutStyle (P0)

```
struct LayoutStyle            // on View, value, ~96 bytes
{
    SizeSpec width, height;   // Fixed(dp|px|%) / Match / Wrap (unchanged semantics)
    Length minWidth, minHeight, maxWidth, maxHeight;   // Auto | Length
    Thickness margin;
    f32 flexGrow = 0, flexShrink = 1;  Length flexBasis = Auto;
    Optional<Align> alignSelf;
    Gravity gravity;          // Frame/Flow/Dock placement (unchanged)
    Dock dock;                // Dock layout side
    Position position = Static | Absolute;  Insets insets;  // absolute in ANY container
    i32 gridRow = -1, gridColumn = -1, gridRowSpan = 1, gridColumnSpan = 1;
    i32 zIndex = 0;
};
```
- `View::Layout()` (the value) + `View::SetLayout(const LayoutStyle&)` mark
  layout damage through the existing `InvalidationKind` routing (the layout-gate
  split of 2026-09-01 stays intact: `zIndex` and `position` are LAYOUT kinds).
- Container-owned data stays on the container: Grid TRACKS (`Columns`/`Rows`/
  `AutoFlow`/spacing), Flex `direction`/`justify`/`align-items`/`wrap`/`gap`,
  Dock `LastChildFill`. Only per-child PLACEMENT moves to the view.
- Layouts read fields directly; the RTTI cast and `CreateDefaultLayoutParams`
  vanish. `MarkupRegistry::RegisterLayoutParam` and the parent-tag lookup go;
  layout attributes are ordinary view attributes named as in section 3.
- Custom container: a `ViewGroup` subclass overriding Measure/Arrange reads
  `child.Layout()` and, when it needs more, `child.CustomProperty("--ring-angle")`.

### 2. Box model on every view (P2)

Every view owns a `VisualStyle` (value) the cascade fills: `background` (solid /
gradient / drawable), `border` per edge (width, color), `border-radius` per
corner, `box-shadow` (offset, blur, spread, color; inset variant), `opacity`
(composited, not per-vertex), `overflow` (visible | hidden | scroll), `cursor`,
`visibility`. Draw order within a container = `z-index` then child order.
Opacity and clipping compose through the VG batch's existing clip stack; shadow
is a blurred rounded-rect drawn by the VG renderer (a new VG draw mode, ONE
shader - the VG DF blur path is the seam).

### 3. Sheet vocabulary + cascade (P1)

- Selectors: type, `.class`, `#id`, `[attr=value]`, pseudo-classes `:hover
  :active :focus :focus-visible :disabled :checked :selected :first-child
  :last-child :empty`, pseudo-elements the controls already expose,
  combinators descendant and child `>` (sibling combinators are OUT).
- Cascade: collect every matching rule, sort by (specificity, source order),
  apply declarations in that order - last wins PER PROPERTY. Replaces the
  per-property best-specificity pick. Computed style is cached per view and
  invalidated by class/pseudo-state change (the existing dirty machinery).
- Inheritance: text properties (color, font-family, font-size, line-height,
  text-align, word-wrap) inherit from the parent's COMPUTED value unless set.
  `inherit` and `initial` keywords.
- Variables: `--name: value` on any rule; `var(--name, fallback)`; stored on
  the view's computed style by `StringHash`; `View::CustomProperty(name)` reads
  it (this is ALSO the custom-layout extension point of ruling 1). The Palette
  becomes a root rule of variables.
- Units: `px`, `dp`, `%` (of the containing box on the same axis), `em` (of the
  computed font size); `calc(a + b)` over lengths with one nesting level.
  `Unit` grows accordingly; SizeSpec::Fixed takes a Length.
- Properties (final names): layout - `width height min-width min-height
  max-width max-height margin(-top/-right/-bottom/-left) flex-grow flex-shrink
  flex-basis align-self gravity dock position top right bottom left grid-row
  grid-column grid-row-span grid-column-span z-index`; container - `flex-
  direction justify-content align-items flex-wrap gap row-gap column-gap grid-
  template-columns grid-template-rows padding`; visual - section 2 list;
  text - `color font-family font-size font-weight line-height text-align
  text-overflow (clip|ellipsis) word-wrap`; motion - section 4.

### 4. Transitions (P3)

`transition: <property> <duration> [<easing>] [<delay>], ...` and
`transition: all 120ms ease-out`. On a computed-style change from a
pseudo-state or class toggle, each listed animatable property (colors, opacity,
lengths, radii, shadow, transform offsets) runs through a `Storyboard` from the
old computed value to the new one; a re-trigger mid-flight retargets from the
CURRENT value (no snap). Layout-kind properties animate through the existing
property routing (they mark layout damage per frame - allowed, but the UA sheet
only transitions VISUAL kinds). The UA default sheet ships `transition: all
120ms ease-out` on the interactive controls; themes may override.

### 5. Flex wrap + gap, ellipsis (P4)

`flex-wrap: nowrap | wrap`, `gap` (both axes), `flex-basis`; lines pack by
`align-content`. `text-overflow: ellipsis` on single-line text views measures
with the font's ellipsis glyph and clips; `word-wrap` keeps today's behaviour.

### 6. Markup

`.sml` attributes ARE the sheet names (`flex-grow="1"`, `align-self="center"`,
`grid-column="2"`, `style="..."` inline). Unknown attribute = load error naming
the element and the attribute. The `MarkupCompletion` provider (toolkit) reads
the same vocabulary table, so completions never drift from the parser.

## Phases (each battery-green, independently landable)

- **P0 - LayoutStyle.** The struct on View; the six layouts read it; delete the
  five LayoutParams classes, `CreateDefaultLayoutParams`, `RegisterLayoutParam`;
  rewrite every `.sml` and every C++ `MakeRef<...LayoutParams>` site in the tree
  (Editor.*, Samples, Tests). Tests: each layout's existing cases pass unchanged
  through the new fields; a custom-container test positions children from
  `Layout()` + a custom property; a reparent test keeps a child's intent.
- **P1 - cascade + vars + units.** Ordered cascade, inheritance, variables,
  units + calc, the attribute selector and the added pseudo-classes. Tests:
  specificity order over 3 competing rules; inheritance through 3 levels;
  var() with fallback; calc; percent against the containing box.
- **P2 - box model.** VisualStyle + shadow/opacity/overflow/z-index/position
  absolute. Tests: draw-order by z-index; an absolute child inside a Flex; a
  shadow emits the DF quad with the right bounds; opacity composes children.
- **P3 - transitions.** Property parsing, the retargeting storyboard, the UA
  sheet defaults. Tests: hover in/out retargets mid-flight; a class toggle
  animates only listed properties; duration 0 = instant.
- **P4 - wrap, gap, ellipsis.** Tests: wrap line count and align-content; gap on
  both axes; ellipsis width never exceeds the box.
- Visual iteration WITH the user at the desk after P2 and P3 (the import dialog
  and the inspector are the yardsticks - both were called "disliked").

## Files (P0 anchors)

`UI/Core/View.cppm` (LayoutParamsPtr -> LayoutStyle), `UI/Layout/*.cppm` (six
layouts + delete LayoutParams.cppm), `UI/Markup/MarkupRegistry.cppm` + `MarkupLoader.cppm`,
`UI/Styling/StyleSheet.cppm` + `StyleSelector.cppm` + `Parser/SSSParser.cppm`,
`UI/Animation/Storyboard.cppm`, `UI.Toolkit/MarkupCompletion.cppm`, every
`.sml`/`.sss` under Data/ and Code/Editor/*, the theme sheets from
ui-theme-migration.md.

## Out of scope (follow-ups, in this order)

Accessibility tree (Godot-level), right-to-left + shaping, data binding (our
adapters stay), in-game sheet hot reload, sibling combinators, CSS grid
templates beyond track sizes.

## Gotchas

- The layout-gate split assumes state rules are colour-only today; after P1 a
  state rule MAY change geometry, so visual-vs-layout classification must come
  from the PROPERTY's InvalidationKind, never from the rule kind.
- One layout, no legacy: no attribute aliases, no LayoutParams shim - a stale
  `.sml` fails to load and says why.
- Beef: the UI port waits for P0-P4 here; porting the old model first would be
  double work.
